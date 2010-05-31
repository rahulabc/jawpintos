#include "threads/synch.h"
#include "filesys/cache.h"
#include <bitmap.h>
#include <inttypes.h>
#include "devices/block.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "filesys/off_t.h"

static struct lock cache_lock;
static struct bitmap *used_slots;

#define CACHE_SIZE 64

struct cache_slot {
  bool accessed;                    // For LRU algorithm
  bool dirty;                       // For write-behind
  struct block *block;              // Filesys block pointer
  block_sector_t sector;            // Sector number
  struct lock cs_lock;              // Lock for this slot
  uint32_t rw_count;                // Num threads read/write
  struct condition no_rw_cond;      // Condition for running evict
  uint8_t data[BLOCK_SECTOR_SIZE];  // Actual Data
  struct inode_disk *disk_inode;    // Inode that uses this
};

static struct cache_slot cache[CACHE_SIZE];

static void 
_cache_slot_init (uint32_t index) 
{
  cache[index].accessed = false;
  cache[index].dirty = false;
  cache[index].block = NULL;
  cache[index].sector = -1;
  cache[index].rw_count = 0;
  cache[index].disk_inode = NULL;
  memset (cache[index].data, 0, BLOCK_SECTOR_SIZE);
}

void 
cache_init (void) 
{
  lock_init (&cache_lock);
  used_slots = bitmap_create (CACHE_SIZE);
  int i = 0;
  for (i = 0; i < CACHE_SIZE; ++i) 
    {
      _cache_slot_init (i);
      lock_init (&cache[i].cs_lock);
      cond_init (&cache[i].no_rw_cond);
    }
}

static uint32_t
_cache_evict (void)
{
  static uint32_t clock_hand = 0;
  while (true) 
    {
      clock_hand++;
      if (clock_hand == CACHE_SIZE)
	clock_hand = 0;
      if (cache[clock_hand].accessed) 
	cache[clock_hand].accessed = false;
      else
	{
	  lock_acquire (&cache[clock_hand].cs_lock);
	  while (cache[clock_hand].rw_count != 0)
	    cond_wait (&cache[clock_hand].no_rw_cond, 
		       &cache[clock_hand].cs_lock);
	  if (cache[clock_hand].dirty)
	    block_write (cache[clock_hand].block,
			 cache[clock_hand].sector,
			 cache[clock_hand].data);
	  lock_release (&cache[clock_hand].cs_lock);
	  _cache_slot_init (clock_hand);
	  return clock_hand;
        }
    }
}

static void 
_cache_fetch (uint32_t index, struct block *block, 
	      block_sector_t sector)
{
  lock_acquire (&cache[index].cs_lock);
  block_read (block, sector, cache[index].data);
  cache[index].block = block;
  cache[index].sector = sector;
  cache[index].rw_count = 0;
  lock_release (&cache[index].cs_lock);
}

static uint32_t 
_cache_find_ensured (struct block *block, block_sector_t sector)
{
  lock_acquire (&cache_lock);
  int i;
  for (i = 0; i < CACHE_SIZE; ++i) 
    {
      if (cache[i].block == block && cache[i].sector == sector)
	{
	  lock_acquire (&cache[i].cs_lock);
	  lock_release (&cache_lock);
	  return i;
	}
    }
  uint32_t index = bitmap_scan_and_flip (used_slots, 0, 1, false); 
  if (index == BITMAP_ERROR)
    index = _cache_evict ();
  _cache_fetch (index, block, sector);
  lock_acquire (&cache[index].cs_lock);
  lock_release (&cache_lock);
  return index;
}

void 
cache_write (struct block *block, block_sector_t sector, 
	     const void *buffer, off_t offset, off_t size,
             struct inode_disk *disk_inode) 
{
  uint32_t index = _cache_find_ensured (block, sector);
  cache[index].rw_count++;
  lock_release (&cache[index].cs_lock);
  
  cache[index].dirty = true;
  cache[index].accessed = true;
  cache[index].disk_inode = disk_inode;
  memcpy (&cache[index].data[offset], buffer, size);
  
  lock_acquire (&cache[index].cs_lock);
  cache[index].rw_count--;
  if (cache[index].rw_count == 0)
    cond_signal (&cache[index].no_rw_cond, &cache[index].cs_lock);
  lock_release (&cache[index].cs_lock);
}

void 
cache_read (struct block *block, block_sector_t sector,
	    void *buffer, off_t offset, off_t size, 
            struct inode_disk *disk_inode)
{
  uint32_t index = _cache_find_ensured (block, sector);
  cache[index].rw_count++;
  lock_release (&cache[index].cs_lock);

  cache[index].accessed = true;
  cache[index].disk_inode = disk_inode;
  memcpy (buffer, &cache[index].data[offset], size);

  lock_acquire (&cache[index].cs_lock);
  cache[index].rw_count--;
  if (cache[index].rw_count == 0)
    cond_signal (&cache[index].no_rw_cond, &cache[index].cs_lock);
  lock_release (&cache[index].cs_lock);
}

void cache_flush (struct block *block, struct inode_disk *disk_inode)
{
  // iterate through all cache slots and flush..
  int i;
  for (i = 0; i < CACHE_SIZE; ++i) 
    {
      if (cache[i].disk_inode == disk_inode)
        {
          // force evict
          lock_acquire (&cache[i].cs_lock);
          while (cache[i].rw_count != 0)
            cond_wait (&cache[i].no_rw_cond, 
                       &cache[i].cs_lock);
          if (cache[i].dirty)
            {
              block_write (cache[i].block,
                           cache[i].sector,
                           cache[i].data);
            }
          lock_release (&cache[i].cs_lock);
          _cache_slot_init (i);
        }
    }
}


