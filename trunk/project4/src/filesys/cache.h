#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"

void cache_read (struct block *, block_sector_t, void *, 
		 off_t, off_t);
void cache_write (struct block *, block_sector_t, const void *,
		  off_t, off_t);
void cache_init (void);

#endif /* filesys/cache.h */
