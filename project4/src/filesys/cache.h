#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"

void cache_read (struct block *, block_sector_t, void *, 
		 off_t, off_t, struct inode_disk *);
void cache_write (struct block *, block_sector_t, const void *,
		  off_t, off_t, struct inode_disk *);
void cache_flush (struct block *, struct inode_disk *);
void cache_init (void);

#endif /* filesys/cache.h */
