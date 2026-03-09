#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define INVALID_SECTOR 4294967290 //4206969696
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCK_COUNT 123
#define INDIRECT_BLOCK_ENTRIES (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))
#define DOUBLE_INDIRECT_BLOCK_ENTRIES (INDIRECT_BLOCK_ENTRIES * INDIRECT_BLOCK_ENTRIES)

#define DIRECT_BLOCK_LIMIT (DIRECT_BLOCK_COUNT * BLOCK_SECTOR_SIZE)
#define INDIRECT_BLOCK_LIMIT (DIRECT_BLOCK_LIMIT + INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE)
#define DOUBLE_INDIRECT_BLOCK_LIMIT                                                                \
  (INDIRECT_BLOCK_LIMIT + DOUBLE_INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE)

static block_sector_t get_indirect_block_sector(block_sector_t indirect_block, off_t pos);
static block_sector_t get_double_indirect_block_sector(block_sector_t double_indirect_block,
                                                       off_t pos);
static bool allocate_double_indirect_block(block_sector_t* double_indirect_block,
                                           size_t blocks_needed);
//bool resize(struct inode_disk *in, off_t size);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */

struct inode_disk {
  block_sector_t direct[DIRECT_BLOCK_COUNT]; // points to 123 blocks
  block_sector_t indirect;                   // 128 blocks
  block_sector_t double_indirect;            // 128 * 128 blocks
  int is_dir;
  off_t length;
  unsigned magic;
  // REMOVED UNUSED
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */

struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct lock lock_inode;
};

static bool get_inode_disk(const struct inode* inode, struct inode_disk* inode_disk) {
  if (inode == NULL || inode_disk == NULL) {
    return false;
  }
  read_cache(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE);
  return true;
}

static block_sector_t get_indirect_block_sector(block_sector_t indirect_block, off_t pos) {
  block_sector_t sectors[INDIRECT_BLOCK_ENTRIES];

  read_cache(indirect_block, &sectors, 0, BLOCK_SECTOR_SIZE);

  // Calculate the index within the indirect block
  size_t index = pos / BLOCK_SECTOR_SIZE;

  return sectors[index];
}

static block_sector_t get_double_indirect_block_sector(block_sector_t double_indirect_block,
                                                       off_t pos) {
  block_sector_t sectors[INDIRECT_BLOCK_ENTRIES];

  read_cache(double_indirect_block, &sectors, 0, BLOCK_SECTOR_SIZE);

  // Calculate the index within the double indirect block
  size_t outer_index = pos / (INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE);
  size_t inner_index = (pos / BLOCK_SECTOR_SIZE) % INDIRECT_BLOCK_ENTRIES;

  block_sector_t indirect_block = sectors[outer_index];

  return get_indirect_block_sector(indirect_block, inner_index * BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */

static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  struct inode_disk inode_d;

  if (!get_inode_disk(inode, &inode_d)) {
    return -1;
  }

  // if (pos >= inode_d.length) {
  //   return -1;
  // }
  if (pos < DIRECT_BLOCK_LIMIT) {
    // Direct block access

    size_t index = pos / BLOCK_SECTOR_SIZE;

    if (inode_d.direct[index] == INVALID_SECTOR) {
      return -1;
    }
    return inode_d.direct[index];
  } else if (pos < INDIRECT_BLOCK_LIMIT) {
    // Indirect block access
    if (inode_d.indirect == INVALID_SECTOR) {
      return -1;
    }

    return get_indirect_block_sector(inode_d.indirect, pos - DIRECT_BLOCK_LIMIT);
  } else if (pos < DOUBLE_INDIRECT_BLOCK_LIMIT) {
    // Double indirect block access
    if (inode_d.double_indirect == INVALID_SECTOR) {
      return -1;
    }
    return get_double_indirect_block_sector(inode_d.double_indirect, pos - INDIRECT_BLOCK_LIMIT);
  }
  // should not reach here, pos too big
  return -1;
}

bool inode_is_dir(const struct inode* inode) {
  struct inode_disk disk;
  get_inode_disk(inode, &disk);
  return disk.is_dir == 1;
}

int inode_open_cnt(struct inode* inode) { return inode->open_cnt; }

void inode_increment_cnt(struct inode* inode) { inode->open_cnt++; }

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* START START START START
Project 3: Buffer Cache */

static struct buffer_cache cache;
static struct lock cache_lock;

// helper to find cache entry given sector number, null if not found
cache_entry_t* find_cache_entry(block_sector_t sector) {
  lock_acquire(&cache_lock);
  for (int i = 0; i < 64; i++) {
    cache_entry_t* entry = &(cache.entries[i]);
    if (entry->valid && entry->sector == sector) {
      lock_release(&cache_lock);
      return entry;
    }
  }
  lock_release(&cache_lock);
  return NULL;
}

// add cache entry for sector with sector's data (optional), evict if cache is full
// return the new cache entry
cache_entry_t* add_cache_entry(block_sector_t sector, bool read_disk) {
  cache_entry_t* new_entry = NULL;
  lock_acquire(&cache_lock);

  // find first invalid cache entry, set new_entry
  for (int i = 0; i < 64; i++) {
    if (!cache.entries[i].valid)
      new_entry = &(cache.entries[i]);
  }
  if (new_entry == NULL) {
    // full cache, need to evict by LRU policy
    new_entry = list_entry(list_back(&cache.lru_order), cache_entry_t, elem);
    flush_cache_entry(new_entry->cache_index);
  }

  // set entry metadata and data (if needed)
  lock_acquire(&new_entry->entry_lock); // is this lock needed if cache_lock is held?
  new_entry->sector = sector;
  new_entry->dirty = false;
  new_entry->valid = true;
  if (read_disk)
    block_read(fs_device, sector, (void*)&new_entry->data);
  lock_release(&new_entry->entry_lock);

  lock_release(&cache_lock);
  return new_entry;
}

// flush cache entry at cache_index to disk
void flush_cache_entry(int cache_index) {
  cache_entry_t* entry = &(cache.entries[cache_index]);
  lock_acquire(&entry->entry_lock);
  if (entry->dirty)
    block_write(fs_device, entry->sector, (void*)&entry->data);
  entry->valid = false;
  lock_release(&entry->entry_lock);
}

void flush_entire_cache() {
  lock_acquire(&cache_lock);
  for (int i = 0; i < 64; i++)
    flush_cache_entry(i);
  lock_release(&cache_lock);
}

// initialize the cache
void init_cache() {
  list_init(&cache.lru_order);
  for (int i = 0; i < 64; i++) {
    cache_entry_t* entry = &(cache.entries[i]);
    entry->cache_index = i; // don't really need this, but it's convenient
    entry->dirty = false;
    entry->valid = false;
    lock_init(&entry->entry_lock);
    list_push_back(&cache.lru_order, &entry->elem);
  }
  lock_init(&cache_lock);
}

// read block at sector number from cache, read from disk if not in cache
// data gets copied to given buffer
void read_cache(block_sector_t sector, void* buffer, off_t offset, int chunk_size) {
  if (sector >= INVALID_SECTOR)
    return;

  cache_entry_t* entry = find_cache_entry(sector);
  if (entry == NULL)
    entry = add_cache_entry(sector, true);

  lock_acquire(&entry->entry_lock);
  memcpy(buffer, (const void*)((uint8_t*)entry->data + offset), chunk_size);

  // Sohom said to acquire cache lock BEFORE releasing entry lock
  lock_acquire(&cache_lock);
  lock_release(&entry->entry_lock);

  // set this entry to most recently used in LRU order
  list_remove(&entry->elem);
  list_push_front(&cache.lru_order, &entry->elem);

  lock_release(&cache_lock);
}

// write buffer with sector's data into cache
void write_cache(block_sector_t sector, void* buffer, off_t offset, int chunk_size) {
  if (sector >= INVALID_SECTOR)
    return;

  cache_entry_t* entry = find_cache_entry(sector);
  if (entry == NULL)
    entry = add_cache_entry(sector, false);

  lock_acquire(&entry->entry_lock);
  memcpy((void*)((uint8_t*)entry->data + offset), (const void*)buffer, chunk_size);
  entry->dirty = true;

  // Sohom said to acquire cache lock BEFORE releasing entry lock
  lock_acquire(&cache_lock);
  lock_release(&entry->entry_lock);

  // set this entry to most recently used in LRU order
  list_remove(&entry->elem);
  list_push_front(&cache.lru_order, &entry->elem);

  lock_release(&cache_lock);
}

/* END END END END END END
Project 3: Buffer Cache */

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
  /* Project 3: Buffer Cache */
  init_cache();
}

// Proj 3
// 0 < blocks_needed <= 128
static bool allocate_indirect_block(block_sector_t* indirect_block, size_t blocks_needed) {
  ASSERT(indirect_block != NULL);

  block_sector_t indirect_block_data[INDIRECT_BLOCK_ENTRIES];

  if (*indirect_block == INVALID_SECTOR) {
    if (!free_map_allocate(1, indirect_block)) {
      return false;
    }
    for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
      indirect_block_data[i] = INVALID_SECTOR;
  } else {
    read_cache(*indirect_block, indirect_block_data, 0, BLOCK_SECTOR_SIZE);
  }

  char zeros[BLOCK_SECTOR_SIZE];
  memset(zeros, 0, BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < blocks_needed; i++) {
    if (indirect_block_data[i] != INVALID_SECTOR)
      continue;
    if (!free_map_allocate(1, &indirect_block_data[i])) {
      // Cleanup on failure
      for (size_t j = 0; j < i; j++) {
        free_map_release(indirect_block_data[j], 1);
      }
      free_map_release(*indirect_block, 1);
      return false;
    } else { // allocation successful
      write_cache(indirect_block_data[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }
  }

  // Write the indirect block data to disk
  write_cache(*indirect_block, indirect_block_data, 0, BLOCK_SECTOR_SIZE);

  return true;
}

// Proj 3
static bool allocate_double_indirect_block(block_sector_t* double_indirect_block,
                                           size_t blocks_needed) {
  ASSERT(double_indirect_block != NULL);

  block_sector_t double_indirect_block_data[INDIRECT_BLOCK_ENTRIES];

  if (*double_indirect_block == INVALID_SECTOR) {
    if (!free_map_allocate(1, double_indirect_block)) {
      return false;
    }
    for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
      double_indirect_block_data[i] = INVALID_SECTOR;
  } else {
    read_cache(*double_indirect_block, double_indirect_block_data, 0, BLOCK_SECTOR_SIZE);
  }

  size_t indirect_blocks_needed = DIV_ROUND_UP(blocks_needed, INDIRECT_BLOCK_ENTRIES);
  for (size_t i = 0; i < indirect_blocks_needed; i++) {
    size_t blocks_to_allocate = min(blocks_needed, INDIRECT_BLOCK_ENTRIES);
    if (!allocate_indirect_block(&double_indirect_block_data[i], blocks_to_allocate)) {
      // Cleanup on failure
      for (size_t j = 0; j < i; j++) {
        free_map_release(double_indirect_block_data[j], 1);
      }
      free_map_release(*double_indirect_block, 1);
      return false;
    }
    blocks_needed -= blocks_to_allocate;
  }

  // Write the double indirect block data to disk
  write_cache(*double_indirect_block, double_indirect_block_data, 0, BLOCK_SECTOR_SIZE);

  return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    // for (int i = 0; i < UNUSED_SPACE_COUNT; i++) {
    //   disk_inode->unused[i] = "A";
    // }
    size_t sectors_needed = bytes_to_sectors(length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    if (is_dir) {
      disk_inode->is_dir = 1;
    } else {
      disk_inode->is_dir = 0;
    }

    // Initialize all pointers to INVALID_SECTOR
    for (int i = 0; i < DIRECT_BLOCK_COUNT; i++) {
      disk_inode->direct[i] = INVALID_SECTOR;
    }
    disk_inode->indirect = INVALID_SECTOR;
    disk_inode->double_indirect = INVALID_SECTOR;

    // Proj 3
    size_t direct_blocks = min(sectors_needed, DIRECT_BLOCK_COUNT);
    size_t indirect_blocks_needed =
        sectors_needed > DIRECT_BLOCK_COUNT
            ? min(sectors_needed - DIRECT_BLOCK_COUNT, INDIRECT_BLOCK_ENTRIES)
            : 0;
    size_t double_indirect_blocks_needed = 0;

    if (DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_ENTRIES < sectors_needed) {
      double_indirect_blocks_needed = sectors_needed - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_ENTRIES;
    }

    // Allocate direct blocks
    for (size_t i = 0; i < direct_blocks; i++) {
      if (!free_map_allocate(1, &disk_inode->direct[i])) {
        free(disk_inode);
        return false;
      }
    }

    // Allocate and initialize indirect block
    if (indirect_blocks_needed > 0) {
      if (!allocate_indirect_block(&disk_inode->indirect, indirect_blocks_needed)) {
        free(disk_inode);
        return false;
      }
    }

    // Allocate and initialize double indirect block
    if (double_indirect_blocks_needed > 0) {
      if (!allocate_double_indirect_block(&disk_inode->double_indirect,
                                          double_indirect_blocks_needed)) {
        free(disk_inode);
        return false;
      }
    }

    // Write inode_disk to disk
    write_cache(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
    success = true;
  }
  free(disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&open_inodes_lock);
      return inode;
    }
  }
  lock_release(&open_inodes_lock);

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock_inode);

  /* Project 3: Buffer Cache */
  // block_read(fs_device, inode->sector, &inode->data);
  struct inode_disk inode_d;
  read_cache(inode->sector, &inode_d, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->lock_inode);
    inode->open_cnt++;
    lock_release(&inode->lock_inode);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode->lock_inode);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    lock_acquire(&open_inodes_lock);
    list_remove(&inode->elem);
    lock_release(&open_inodes_lock);

    /* Fetch inode_disk data to handle deallocation if removed. */
    struct inode_disk inode_d;
    read_cache(inode->sector, &inode_d, 0, BLOCK_SECTOR_SIZE);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      // Deallocate direct blocks
      for (int i = 0; i < DIRECT_BLOCK_COUNT; i++) {
        if (inode_d.direct[i] != INVALID_SECTOR) { // Assuming 0 is an invalid sector
          free_map_release(inode_d.direct[i], 1);
        }
      }

      // Deallocate indirect blocks
      if (inode_d.indirect != INVALID_SECTOR) {
        block_sector_t indirect_blocks[INDIRECT_BLOCK_ENTRIES];
        read_cache(inode_d.indirect, &indirect_blocks, 0, BLOCK_SECTOR_SIZE);
        for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++) {
          if (indirect_blocks[i] != INVALID_SECTOR) {
            free_map_release(indirect_blocks[i], 1);
          }
        }
        free_map_release(inode_d.indirect, 1);
      }

      // Deallocate double indirect blocks
      if (inode_d.double_indirect != INVALID_SECTOR) {
        block_sector_t double_indirect_blocks[INDIRECT_BLOCK_ENTRIES];
        read_cache(inode_d.double_indirect, &double_indirect_blocks, 0, BLOCK_SECTOR_SIZE);
        for (int i = 0; i < INDIRECT_BLOCK_ENTRIES; i++) {
          if (double_indirect_blocks[i] != INVALID_SECTOR) {
            block_sector_t indirect_blocks[INDIRECT_BLOCK_ENTRIES];
            read_cache(double_indirect_blocks[i], &indirect_blocks, 0, BLOCK_SECTOR_SIZE);
            for (int j = 0; j < INDIRECT_BLOCK_ENTRIES; j++) {
              if (indirect_blocks[j] != INVALID_SECTOR) {
                free_map_release(indirect_blocks[j], 1);
              }
            }
            free_map_release(double_indirect_blocks[i], 1);
          }
        }
        free_map_release(inode_d.double_indirect, 1);
      }
    }
    lock_release(&inode->lock_inode);
    free(inode);
    inode = NULL;
  }
  if (inode != NULL)
    lock_release(&inode->lock_inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->lock_inode);
  inode->removed = true;
  lock_release(&inode->lock_inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  if (inode == NULL)
    return 0;
  // uint8_t* bounce = NULL;
  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    if (sector_idx == -1)
      break;
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */

      /* Project 3: Buffer Cache */
      // block_read(fs_device, sector_idx, buffer + bytes_read);
      read_cache(sector_idx, buffer + bytes_read, 0, BLOCK_SECTOR_SIZE);
    } else {
      /* Project 3: Buffer Cache */
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. (Project 3: remove bounce) */
      // if (bounce == NULL) {
      //   bounce = malloc(BLOCK_SECTOR_SIZE);
      //   if (bounce == NULL)
      //     break;
      // }
      // block_read(fs_device, sector_idx, bounce);
      // memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
      read_cache(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  // free(bounce);

  return bytes_read;
}

bool inode_resize(struct inode_disk* disk_inode, off_t size) {
  bool success = true;

  // Handles direct pointers
  for (int i = 0; i < DIRECT_BLOCK_COUNT; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && disk_inode->direct[i] != INVALID_SECTOR) {
      //deallocate_block(in->direct[i]);
      disk_inode->direct[i] = INVALID_SECTOR;
    } else if (size > BLOCK_SECTOR_SIZE * i && disk_inode->direct[i] == INVALID_SECTOR) {
      if (!free_map_allocate(1, &disk_inode->direct[i])) {
        success = false;
        break;
      }
    }
  }

  // Rollback if needed
  if (!success) {
    inode_resize(disk_inode, disk_inode->length); // Roll back to original size
    return false;
  }

  // Calculate number of blocks needed for indirect and double-indirect blocks
  size_t sectors_needed = bytes_to_sectors(size);
  size_t indirect_blocks_needed =
      sectors_needed > DIRECT_BLOCK_COUNT
          ? min(sectors_needed - DIRECT_BLOCK_COUNT, INDIRECT_BLOCK_ENTRIES)
          : 0;
  size_t double_indirect_blocks_needed = 0;
  if (DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_ENTRIES < sectors_needed) {
    double_indirect_blocks_needed = sectors_needed - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_ENTRIES;
  }

  // Indirect pointers handling
  if (indirect_blocks_needed > 0) {
    success = allocate_indirect_block(&disk_inode->indirect, indirect_blocks_needed);
  } else {
    //deallocate_indirect_block(in->indirect);
    disk_inode->indirect = INVALID_SECTOR;
  }

  // Doubly Indirect pointers handling
  if (double_indirect_blocks_needed > 0) {
    // if (disk_inode->double_indirect == INVALID_SECTOR && !free_map_allocate(1, &disk_inode->double_indirect)) {
    //   return false;
    // }
    success =
        allocate_double_indirect_block(&disk_inode->double_indirect, double_indirect_blocks_needed);
  } else {
    //deallocate_double_indirect_block(in->double_indirect);
    disk_inode->double_indirect = INVALID_SECTOR;
  }

  // Update inode length and return success
  if (success) {
    // disk_inode->length = size;
  }
  return success;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;

  off_t size_copy = size;
  off_t offset_copy = offset;
  // uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // Proj 3

  struct inode_disk disk_inode;
  read_cache(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE);

  if (disk_inode.length < offset + size) {
    if (!inode_resize(&disk_inode, offset + size))
      return 0;
    lock_acquire(&inode->lock_inode);
    write_cache(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE);
    lock_release(&inode->lock_inode);
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    if (sector_idx == -1)
      break;
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    // off_t inode_left = inode_length(inode) - offset;
    off_t inode_left = offset_copy + size_copy - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */

      /* Project 3: Buffer Cache */
      // block_write(fs_device, sector_idx, buffer + bytes_written);
      write_cache(sector_idx, buffer + bytes_written, 0, BLOCK_SECTOR_SIZE);
    } else {
      /* Project 3: Buffer Cache */
      /* We need a bounce buffer. (Project 3: remove bounce) */
      // if (bounce == NULL) {
      //   bounce = malloc(BLOCK_SECTOR_SIZE);
      //   if (bounce == NULL)
      //     break;
      // }

      // /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      // if (sector_ofs > 0 || chunk_size < sector_left)
      //   block_read(fs_device, sector_idx, bounce);
      // else
      //   memset(bounce, 0, BLOCK_SECTOR_SIZE);
      // memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      // block_write(fs_device, sector_idx, bounce);
      write_cache(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      // block read is done within write_cache through add_cache_entry if needed
      // sector of all zeros is when sector_ofs==0 and chunk_size==BLOCK_SECTOR_SIZE
      // so for cache write we don't need special handling (?)
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  // free(bounce);
  if (disk_inode.length < offset_copy + size_copy) {
    disk_inode.length = offset_copy + size_copy;
    lock_acquire(&inode->lock_inode);
    write_cache(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE);
    lock_release(&inode->lock_inode);
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  lock_acquire(&inode->lock_inode);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock_inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  lock_acquire(&inode->lock_inode);
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock_inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  if (inode == NULL) {
    return -1;
  }

  lock_acquire(&inode->lock_inode);
  struct inode_disk inode_d;
  read_cache(inode->sector, &inode_d, 0, BLOCK_SECTOR_SIZE);
  lock_release(&inode->lock_inode);

  return inode_d.length;
}
