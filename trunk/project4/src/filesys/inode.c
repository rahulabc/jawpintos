#include "filesys/inode.h"
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include <stdio.h>
#include "filesys/fsutil.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MI_SIZE 124
#define MI_NUM_DIRECT 100
#define MI_NUM_DOUBLY_INDIRECT 1
#define MI_NUM_INDIRECT (MI_SIZE - MI_NUM_DIRECT - MI_NUM_DOUBLY_INDIRECT)
#define INVALID_SECTOR_INDEX ((block_sector_t) (-1))

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static const int SECTORS_PER_BLOCK = 
  BLOCK_SECTOR_SIZE / sizeof (block_sector_t);
  
block_sector_t
get_sector (struct inode_disk *disk_inode, 
	    block_sector_t block_index)
{
  ASSERT (disk_inode->length < 
	  8 * 1024 * 1024 - MI_NUM_INDIRECT * BLOCK_SECTOR_SIZE - 
	  MI_NUM_DOUBLY_INDIRECT * BLOCK_SECTOR_SIZE - 
	  MI_NUM_DOUBLY_INDIRECT * SECTORS_PER_BLOCK * BLOCK_SECTOR_SIZE);
  
  /* REVISIT: validate return buffer */
  if (block_index < MI_NUM_DIRECT)    
    {
      /* DIRECT */
      return disk_inode->multi_index[block_index];
    }
  else if (block_index < MI_NUM_DIRECT + MI_NUM_INDIRECT * SECTORS_PER_BLOCK) 
    {
      /* INDIRECT */
      block_index -= MI_NUM_DIRECT;
      block_sector_t buffer[SECTORS_PER_BLOCK];
      
      block_sector_t level_1_index = 
	MI_NUM_DIRECT + block_index / SECTORS_PER_BLOCK;
      if (disk_inode->multi_index[level_1_index] == INVALID_SECTOR_INDEX)
	return INVALID_SECTOR_INDEX;

      cache_read (fs_device, disk_inode->multi_index[level_1_index],
		  buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
      return buffer[block_index % SECTORS_PER_BLOCK];
    }
  else
    {
      /* DOUBLY INDIRECT */
      block_index -= MI_NUM_DIRECT + MI_NUM_INDIRECT * SECTORS_PER_BLOCK;
      block_sector_t buffer[SECTORS_PER_BLOCK];
      
      block_sector_t level_1_index = MI_SIZE - MI_NUM_DOUBLY_INDIRECT;
      if (disk_inode->multi_index[level_1_index] == INVALID_SECTOR_INDEX)
	return INVALID_SECTOR_INDEX;

      cache_read (fs_device, disk_inode->multi_index[level_1_index],
		  buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);

      block_sector_t level_2_index = block_index / SECTORS_PER_BLOCK;
      if (buffer[level_2_index] == INVALID_SECTOR_INDEX)
	return INVALID_SECTOR_INDEX;

      cache_read (fs_device, buffer[level_2_index],
		  buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
      return buffer[block_index % SECTORS_PER_BLOCK];
    }
}

static block_sector_t
_create_new_sector_ptr_block (struct inode_disk *disk_inode)
{
  block_sector_t sector_tmp;
  if (!free_map_allocate_one (&sector_tmp))
    return INVALID_SECTOR_INDEX;
  block_sector_t buffer[SECTORS_PER_BLOCK];

  int idx;
  for (idx = 0; idx < SECTORS_PER_BLOCK; ++idx)
    buffer[idx] = INVALID_SECTOR_INDEX;
  cache_write (fs_device, sector_tmp, buffer, 
	       0, BLOCK_SECTOR_SIZE, disk_inode);
  return sector_tmp;
}

void
put_sector (struct inode_disk *disk_inode,
	    block_sector_t block_index, block_sector_t sector)
{
  ASSERT (disk_inode->length < 
	  8 * 1024 * 1024 - MI_NUM_INDIRECT * BLOCK_SECTOR_SIZE - 
	  MI_NUM_DOUBLY_INDIRECT * BLOCK_SECTOR_SIZE - 
	  MI_NUM_DOUBLY_INDIRECT * SECTORS_PER_BLOCK * BLOCK_SECTOR_SIZE);
  
  /* REVISIT: validate return buffer */
  if (block_index < MI_NUM_DIRECT)    
    {
      /* DIRECT */
      disk_inode->multi_index[block_index] = sector;
    }
  else if (block_index < MI_NUM_DIRECT + MI_NUM_INDIRECT * SECTORS_PER_BLOCK) 
    {
      /* INDIRECT */
      block_index -= MI_NUM_DIRECT;
      block_sector_t buffer[SECTORS_PER_BLOCK];
      block_sector_t level_1_index = MI_NUM_DIRECT + 
	                             block_index / SECTORS_PER_BLOCK;

      if (disk_inode->multi_index[level_1_index] == INVALID_SECTOR_INDEX)
	disk_inode->multi_index[level_1_index] = 
	  _create_new_sector_ptr_block (disk_inode);

      cache_read (fs_device, disk_inode->multi_index[level_1_index],
		  buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
      buffer[block_index % SECTORS_PER_BLOCK] = sector;
      cache_write (fs_device, disk_inode->multi_index[level_1_index],
		   buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
    }
  else
    {
      /* DOUBLY INDIRECT */
      block_index -= MI_NUM_DIRECT + MI_NUM_INDIRECT * SECTORS_PER_BLOCK;
      block_sector_t buffer[SECTORS_PER_BLOCK];
      block_sector_t level_1_index = MI_SIZE - MI_NUM_DOUBLY_INDIRECT;
      if (disk_inode->multi_index[level_1_index] == INVALID_SECTOR_INDEX)
	disk_inode->multi_index[level_1_index] =
	  _create_new_sector_ptr_block (disk_inode);

      cache_read (fs_device, disk_inode->multi_index[level_1_index],
		  buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
      
      block_sector_t level_2_index = block_index / SECTORS_PER_BLOCK;
      if (buffer[level_2_index] == INVALID_SECTOR_INDEX)
	buffer[level_2_index] = 
	  _create_new_sector_ptr_block (disk_inode);

      block_sector_t tmp = buffer[block_index / SECTORS_PER_BLOCK];
      cache_read (fs_device, tmp, buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
      buffer[block_index % SECTORS_PER_BLOCK] = sector;
      cache_write (fs_device, tmp, buffer, 0, BLOCK_SECTOR_SIZE, disk_inode);
    }
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  off_t block_index = pos / BLOCK_SECTOR_SIZE;
  if (pos >= inode->data.length)
    return INVALID_SECTOR_INDEX;
  return get_sector (&inode->data, block_index);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);  

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = false;
      disk_inode->parent_dir_sector = ROOT_DIR_SECTOR;

      /* intialize the multi-level index table as all INVALID_SECTOR_INDEX 
	 because it is an empty file now */
      int idx;
      for (idx = 0; idx < MI_SIZE; ++idx)
	disk_inode->multi_index[idx] = INVALID_SECTOR_INDEX;

      if (free_map_allocate_multiple (sectors, 0, disk_inode))
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++)
                block_write (fs_device, get_sector (disk_inode, i), zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{ 
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  
  lock_init (&inode->extension_lock);

  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{ 
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Flush cache */
      cache_flush (&inode->data);
      block_write (fs_device, inode->sector, &inode->data);
 
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (byte_to_sector(inode, 0),
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      if (offset >= inode_length(inode))
        break;
  
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read (fs_device, sector_idx, buffer + bytes_read,
		  sector_ofs, chunk_size, &inode->data);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

static off_t
extend_and_write (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  lock_acquire (&inode->extension_lock);
  const uint8_t *buffer = buffer_;

  if (inode->deny_write_cnt)
    {
      lock_release (&inode->extension_lock);
      return 0;
    }
  off_t bytes_written = 0;
  uint8_t zeros[BLOCK_SECTOR_SIZE];
  memset (zeros, 0, BLOCK_SECTOR_SIZE);
  while (size > 0) 
    {
      off_t flength = inode_length (inode);
      block_sector_t sector_idx = get_sector (&inode->data, 
					      flength/BLOCK_SECTOR_SIZE);
      off_t block_start = flength % BLOCK_SECTOR_SIZE;
      if (sector_idx == INVALID_SECTOR_INDEX)
        {
          if (free_map_allocate_multiple (1, flength / BLOCK_SECTOR_SIZE, 
					  &inode->data))
	    sector_idx = get_sector (&inode->data, flength/BLOCK_SECTOR_SIZE);
	  else
            {
              lock_release (&inode->extension_lock);
              return bytes_written;
            }
        }
      off_t block_left = BLOCK_SECTOR_SIZE - block_start;
      cache_write (fs_device, sector_idx, zeros + block_start, 
                   block_start, block_left, &inode->data);

      if (sector_idx == get_sector (&inode->data, 
				    offset / BLOCK_SECTOR_SIZE))
        {
          int sector_ofs = offset % BLOCK_SECTOR_SIZE; 
          int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
          int chunk_size = size < sector_left ? size : sector_left;

          cache_write (fs_device, sector_idx, buffer + bytes_written,
                       sector_ofs, chunk_size, &inode->data);

          size -= chunk_size;
          offset += chunk_size;
	  inode->data.length += chunk_size;
	  if (block_start != sector_ofs)
	    inode->data.length += sector_ofs;
          bytes_written += chunk_size;
        }
      else
        inode->data.length += block_left;
    }

  lock_release (&inode->extension_lock);
  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      if (offset >= inode_length (inode))
        {
          bytes_written += extend_and_write (inode, buffer + bytes_written, 
					     size, offset);
          return bytes_written;
        }
          
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write (fs_device, sector_idx, buffer + bytes_written,
		   sector_ofs, chunk_size, &inode->data);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
inode_is_dir (const struct inode *inode)
{
   return inode->data.is_dir;
}

void
inode_set_is_dir (struct inode *inode)
{
   inode->data.is_dir = true;
   block_write (fs_device, inode->sector, &inode->data);
}

void
inode_set_parent_dir_sector (struct inode *inode, block_sector_t parent_dir_sector)
{
   inode->data.parent_dir_sector = parent_dir_sector;
}

block_sector_t
inode_get_parent_dir_sector (struct inode *inode)
{
   return inode->data.parent_dir_sector;
}

