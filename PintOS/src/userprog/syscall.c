#include "userprog/syscall.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "lib/float.h"

// struct lock global_file_lock;

static void syscall_handler(struct intr_frame*);
static int sys_practice(int i);
static void sys_halt(void);
static pid_t sys_exec(const char* cmd_line);
static int sys_wait(pid_t pid);
static bool sys_create(const char* filename, unsigned initial_size);
static bool sys_remove(const char* filename);
static int sys_open(const char* filename);
static int sys_filesize(int fd);
static int sys_read(int fd, void* buffer, unsigned size);
static int sys_write(int fd, const void* buffer, unsigned size);
static void sys_seek(int fd, unsigned position);
static int sys_tell(int fd);
static void sys_close(int fd);
static bool valid_pointer(const void* pointer, unsigned size, bool check_mapped);
static bool valid_string(const char* pointer);
static file_elem_t* find_file_elem(struct list* fdt, file_descriptor fd);
static int sys_compute_e(int n);
static bool sys_chdir(const char* dir);
static bool sys_mkdir(const char* dir);
static bool sys_readdir(int fd, char* name);
static bool sys_isdir(int fd);
static int sys_inumber(int fd);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  // lock_init(&global_file_lock);
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */
  // printf("System call number: %d\n", args[0]);

  // check args[0]
  if (!valid_pointer((void*)args, 4, true))
    return;
  switch (args[0]) {
    case SYS_PRACTICE:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_practice(args[1]);
      break;
    case SYS_HALT:
      sys_halt();
      break;
    case SYS_EXIT:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = args[1];
      sys_exit(args[1]);
      break;
    case SYS_EXEC:
      if (!valid_pointer((void*)args, 8, true))
        return;
      f->eax = sys_exec((char*)args[1]);
      break;
    case SYS_WAIT:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_wait((pid_t)args[1]);
      break;
    case SYS_CREATE: // File syscalls begin here
      if (!valid_pointer((void*)args, 12, false))
        return;
      f->eax = (int)sys_create((char*)args[1], args[2]);
      break;
    case SYS_REMOVE:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = (int)sys_remove((char*)args[1]);
      break;
    case SYS_OPEN:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_open((char*)args[1]);
      break;
    case SYS_FILESIZE:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_filesize(args[1]);
      break;
    case SYS_READ:
      if (!valid_pointer((void*)args, 16, false))
        return;
      f->eax = sys_read(args[1], (void*)args[2], args[3]);
      break;
    case SYS_WRITE:
      if (!valid_pointer((void*)args, 16, false))
        return;
      f->eax = sys_write(args[1], (const void*)args[2], args[3]);
      break;
    case SYS_SEEK:
      if (!valid_pointer((void*)args, 12, false))
        return;
      sys_seek(args[1], args[2]);
      break;
    case SYS_TELL:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_tell(args[1]);
      break;
    case SYS_CLOSE:
      if (!valid_pointer((void*)args, 8, false))
        return;
      sys_close(args[1]);
      break;
    case SYS_COMPUTE_E:
      if (!valid_pointer((void*)args, 8, false))
        return;

      if (args[1] > 0) {
        f->eax = sys_compute_e(args[1]);
        break;
      } else {
        return;
      }
    case SYS_CHDIR:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_chdir(args[1]);
      break;
    case SYS_MKDIR:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_mkdir(args[1]);
      break;
    case SYS_READDIR:
      if (!valid_pointer((void*)args, 12, false))
        return;
      f->eax = sys_readdir(args[1], args[2]);
      break;
    case SYS_ISDIR:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_isdir(args[1]);
      break;
    case SYS_INUMBER:
      if (!valid_pointer((void*)args, 8, false))
        return;
      f->eax = sys_inumber(args[1]);
      break;
    default:
      // printf("Invalid syscall number: %d\n", args[0]);
      return;
  }
}

/***********************
PROCESS CONTROL SYSCALLS 
***********************/

static int sys_practice(int i) { return i + 1; }

static void sys_halt(void) {
  printf("Shutting power off");
  shutdown_power_off();
}

void sys_exit(int status) {
  // Close all files in FDT and free all file_elems
  struct list* fdt = &(thread_current()->pcb->file_desc_table);
  struct list_elem* e;
  file_elem_t* file_el;
  while (!list_empty(fdt)) {
    e = list_pop_front(fdt);
    file_el = list_entry(e, file_elem_t, elem);

    // if (!lock_held_by_current_thread(&global_file_lock))
    //   lock_acquire(&global_file_lock);
    file_close(file_el->fp);
    // lock_release(&global_file_lock);

    free(file_el);
  }

  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit(status);
}

static pid_t sys_exec(const char* cmd_line) {
  if (!valid_pointer((void*)cmd_line, 4, true))
    return -1;
  if (!valid_string(cmd_line)) {
    return -1;
  }

  pid_t pid = process_execute(cmd_line);
  return pid;
}

static int sys_wait(pid_t pid) {
  int exit_code = process_wait(pid);
  return exit_code;
}

/**********************
FILE OPERATION SYSCALLS 
**********************/

static bool sys_create(const char* filename, unsigned initial_size) {
  if (!valid_string(filename)) {
    return false;
  }
  // lock_acquire(&global_file_lock);
  bool success = filesys_create(filename, initial_size, false);
  // lock_release(&global_file_lock);
  return success;
}

static bool sys_remove(const char* filename) {
  if (!valid_string(filename)) {
    return false;
  }
  // lock_acquire(&global_file_lock);
  bool success = filesys_remove(filename);
  // lock_release(&global_file_lock);
  return success;
}

static int sys_open(const char* filename) {
  if (!valid_string(filename)) {
    return false;
  }
  // lock_acquire(&global_file_lock);
  struct file* file = filesys_open(filename);
  if (strcmp(filename, thread_current()->pcb->process_name) == 0) {
    file_deny_write(file);
  }
  // lock_release(&global_file_lock);
  if (file == NULL) {
    return -1;
  }
  // Create and add new file_elem to FDT
  file_elem_t* elem = (file_elem_t*)malloc(sizeof(file_elem_t));
  if (elem == NULL) {
    return -1;
  }
  struct process* pcb = thread_current()->pcb;
  elem->fd = pcb->next_fd++;
  if (inode_is_dir(file_get_inode(file))) {
    elem->is_dir = true;
    elem->dp = filesys_open_dir(filename);
    elem->fp = NULL;
  } else {
    elem->is_dir = false;
    elem->fp = file;
    elem->dp = NULL;
  }
  list_push_back(&(pcb->file_desc_table), &(elem->elem));
  return elem->fd;
}

static int sys_filesize(int fd) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL) {
    return -1;
  }
  // lock_acquire(&global_file_lock);
  int length = file_length(elem->fp);
  // lock_release(&global_file_lock);
  return length;
}

static int sys_read(int fd, void* buffer, unsigned size) {
  if (!valid_pointer(buffer, size + 1, true)) {
    return false;
  }
  if (fd == 0) { // read from stdin
    char c;
    for (unsigned i = 0; i < size; i++) {
      c = input_getc();
      memset(buffer + i, c, 1);
    }
    return size;
  }
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL || elem->is_dir) {
    return -1;
  }
  // lock_acquire(&global_file_lock);
  int bytes_read = file_read(elem->fp, buffer, size);
  // lock_release(&global_file_lock);
  return bytes_read;
}

static int sys_write(int fd, const void* buffer, unsigned size) {
  if (!valid_pointer(buffer, size + 1, true)) {
    return false;
  }
  unsigned bytes_written = 0;
  if (fd == 1) { // write to stdout
    const unsigned console_chunk_size = 256;
    unsigned remaining_bytes;
    while (bytes_written < size) {
      remaining_bytes = size - bytes_written;
      if (remaining_bytes >= console_chunk_size) {
        putbuf(buffer + bytes_written, console_chunk_size);
        bytes_written += console_chunk_size;
      } else {
        putbuf(buffer + bytes_written, remaining_bytes);
        bytes_written += remaining_bytes;
      }
    }
  } else {
    file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
    if (elem == NULL || elem->is_dir) {
      return -1;
    }
    // lock_acquire(&global_file_lock);
    bytes_written = file_write(elem->fp, buffer, size);
    // lock_release(&global_file_lock);
  }
  return bytes_written;
}

static void sys_seek(int fd, unsigned position) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL) {
    return;
  }
  // lock_acquire(&global_file_lock);
  file_seek(elem->fp, position);
  // lock_release(&global_file_lock);
}

static int sys_tell(int fd) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL) {
    return -1;
  }
  // lock_acquire(&global_file_lock);
  int pos = file_tell(elem->fp);
  // lock_release(&global_file_lock);
  return pos;
}

static void sys_close(int fd) {
  struct process* pcb = thread_current()->pcb;
  file_elem_t* elem = find_file_elem(&(pcb->file_desc_table), fd);
  if (elem == NULL) {
    return;
  }
  // lock_acquire(&global_file_lock);
  file_close(elem->fp);
  list_remove(&(elem->elem));
  free(elem);
  // lock_release(&global_file_lock);
}

/***************
FPU Syscall
***************/

static int sys_compute_e(int n) { return sys_sum_to_e(n); }

/***************
Filesys Syscall
***************/

static bool sys_chdir(const char* dir) {
  struct dir* dp = filesys_open_dir(dir);

  if (dp == NULL)
    return false;

  struct dir* prev = thread_current()->pcb->cwd;
  thread_current()->pcb->cwd = dp;
  if (prev != NULL)
    dir_close(prev);

  return true;
}

static bool sys_mkdir(const char* dir) {
  if (!valid_string(dir)) {
    return false;
  }
  bool success = filesys_create(dir, 0, true);

  return success;
}

static bool sys_readdir(int fd, char* name) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL || !elem->is_dir) {
    return false;
  }
  return dir_readdir(elem->dp, name);
}

static bool sys_isdir(int fd) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL || !elem->is_dir) {
    return false;
  }
  return elem->is_dir;
}

static int sys_inumber(int fd) {
  file_elem_t* elem = find_file_elem(&(thread_current()->pcb->file_desc_table), fd);
  if (elem == NULL) {
    return -1;
  }
  if (elem->is_dir) {
    return inode_get_inumber(dir_get_inode(elem->dp));
  } else {
    return inode_get_inumber(file_get_inode(elem->fp));
  }
}

/***************
HELPER FUNCTIONS
***************/

/* Returns true iff pointer with given size is valid. */
static bool valid_pointer(const void* pointer, unsigned size, bool check_mapped) {
  if (pointer == NULL) {
    sys_exit(-1);
    return false;
  }
  if (pointer < 0) {
    sys_exit(-1);
    return false;
  }
  if (!is_user_vaddr(pointer + size)) {
    sys_exit(-1);
    return false;
  }
  if (check_mapped && pagedir_get_page(thread_current()->pcb->pagedir, pointer + size) == NULL) {
    sys_exit(-1);
    return false;
  }
  return true;
}

static bool valid_string(const char* pointer) {
  if (pointer == NULL) {
    sys_exit(-1);
    return false;
  }
  if (pointer < 0) {
    sys_exit(-1);
    return false;
  }
  if (!is_user_vaddr(pointer)) {
    sys_exit(-1);
    return false;
  }
  if (pagedir_get_page(thread_current()->pcb->pagedir, pointer) == NULL) {
    sys_exit(-1);
    return false;
  }
  unsigned size = strlen(pointer) + 1;
  if (!is_user_vaddr(pointer + size)) {
    sys_exit(-1);
    return false;
  }
  if (pagedir_get_page(thread_current()->pcb->pagedir, pointer + size) == NULL) {
    sys_exit(-1);
    return false;
  }
  return true;
}

/* Return the file_elem with given file descriptor in the FDT */
static file_elem_t* find_file_elem(struct list* fdt, file_descriptor fd) {
  struct list_elem* e;
  file_elem_t* file;
  for (e = list_begin(fdt); e != list_end(fdt); e = list_next(e)) {
    file = list_entry(e, file_elem_t, elem);
    if (file->fd == fd) {
      return file;
    }
  }
  return NULL;
}
