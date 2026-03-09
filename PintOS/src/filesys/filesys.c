#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
  struct dir* root = dir_open_root();
  dir_add(root, ".", ROOT_DIR_SECTOR);
  dir_add(root, "..", ROOT_DIR_SECTOR);
  dir_close(root);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  /* Project 3: Buffer Cache */
  free_map_close();

  // for (int i = 0; i < 64; i++)
  //   flush_cache_entry(i);
  flush_entire_cache();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size, bool is_dir) {
  block_sector_t inode_sector = 0;
  char* filename = (char*)malloc(sizeof(char) * (NAME_MAX + 1));
  struct dir* dir = dir_open_path(name, filename);
  bool success =
      (dir != NULL && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size, is_dir) && dir_add(dir, filename, inode_sector));
  free(filename);
  if (!success && inode_sector != 0) {
    free_map_release(inode_sector, 1);
    dir_close(dir);
    return success;
  }
  if (is_dir && dir != NULL) {
    struct dir* new_dir = dir_open(inode_open(inode_sector));
    dir_add(new_dir, ".", inode_sector);
    dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
    dir_close(new_dir);
  }
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  char* filename = (char*)malloc(sizeof(char) * (NAME_MAX + 1));
  struct dir* dir = dir_open_path(name, filename);
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, filename, &inode);
  dir_close(dir);
  free(filename);
  return file_open(inode);
}

/* Opens the directory with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct dir* filesys_open_dir(const char* name) {
  char* filename = (char*)malloc(sizeof(char) * (NAME_MAX + 1));
  struct dir* dir = dir_open_path(name, filename);
  struct inode* inode = NULL;

  if (strcmp(filename, "."))
    dir = NULL;
  free(filename);
  return dir;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
  char* filename = (char*)malloc(sizeof(char) * (NAME_MAX + 1));
  struct dir* dir = dir_open_path(name, filename);
  if (strcmp(".", filename) == 0) {
    if (inode_open_cnt(dir_get_inode(dir)) != 1 ||
        inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR) {
      return false;
    }
    if (dir_empty(dir)) {
      struct dir* parent = dir_parent(dir);
      char* name_last = get_last_part(&name);
      bool success = dir_remove(parent, name_last);
      free(name_last);
      dir_close(parent);
      return success;
    } else {
      return false;
    }
  } else {
    bool success = dir != NULL && dir_remove(dir, filename);
    dir_close(dir);

    return success;
  }
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}
