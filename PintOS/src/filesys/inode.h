#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

struct bitmap;

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(const struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
off_t inode_length(const struct inode*);
bool inode_is_dir(const struct inode*);

#endif /* filesys/inode.h */

/* Project 3: Buffer Cache */
typedef struct cache_entry {
  int cache_index;              // index of cache_entry
  block_sector_t sector;        // sector number
  bool dirty;                   // false = clean, true = dirty
  bool valid;                   // false = invalid, true = valid
  struct list_elem elem;        // for use in LRU_order list
  struct lock entry_lock;       // serialize accesses to same sector
  char data[BLOCK_SECTOR_SIZE]; // static buffer with size exactly one block
} cache_entry_t;

typedef struct buffer_cache {
  cache_entry_t entries[64]; // array of 64 cache entries
  struct list lru_order;     // cache entries ordered by used time, list of indices
                             // front of list = most recent, back = least recent
} buffer_cache_t;

cache_entry_t* find_cache_entry(block_sector_t);
cache_entry_t* add_cache_entry(block_sector_t, bool);
void flush_cache_entry(int);
void flush_entire_cache(void);
void init_cache(void);
void read_cache(block_sector_t, void*, off_t, int);
void write_cache(block_sector_t, void*, off_t, int);