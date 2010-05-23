#include "vm/swap.h"
#include "vm/page.h"
#include "vm/frame.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <string.h>


static struct block *swap_partition;
static struct bitmap *swap_table;
static const int NUM_SECTORS_IN_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

void 
swap_init (void)
{
  swap_partition = block_get_role (BLOCK_SWAP);

  /* number of pages in the swap partition */
  uint32_t bitmap_size = block_size (swap_partition) * 
    BLOCK_SECTOR_SIZE / PGSIZE;  
  swap_table = bitmap_create (bitmap_size);
  clock_hand = list_head (&frame_table);
}

/* Copies a user page specified by UPAGE that is stored in 
   swap disk to physical frame KPAGE */
bool
swap_fetch (tid_t tid, uint32_t *upage, uint32_t *kpage)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);
  if (sde == NULL)
    return false;
  struct spt_element *se = spt_find (sde, upage);
  if (se == NULL)
    return false;
  if (se->source != FRAME_SWAP)
    return false;
  
  uint32_t swap_index = se->swap_index;
  uint32_t sector_no = swap_index * NUM_SECTORS_IN_PAGE;
  int i;

  /* Read a page from swap partition to memory block by block */
  for (i = 0; i < NUM_SECTORS_IN_PAGE; ++i)
    block_read (swap_partition, sector_no + i, 
		(char *)kpage + BLOCK_SECTOR_SIZE * i);

  /* supp page table and page table update */
  spt_pagedir_update (get_thread (tid), upage, kpage, FRAME_FRAME,
		      0, NULL, 0, 0, 0, true);

  /* frame table update */
  frame_element_set (frame_table_find (kpage), upage, tid);

  /* swap table update */
  bitmap_set (swap_table, swap_index, false);
  return true;
}

/* Selects a frame to evict using heuristic LRU method and copies
   the user page stored in physical frame KPAGE to the swap disk. */ 
void *
swap_evict (void)
{
  while (true)
    {
      /* move clock_hand around the frame table */
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_table))
	{
	  clock_hand = list_head (&frame_table);
	  continue;
	}

      struct frame_element *fe = list_entry (clock_hand, struct frame_element,
					     frame_elem);
      ASSERT (fe != NULL);

      struct thread *t = get_thread (fe->tid);

      if (pagedir_is_accessed (t->pagedir, fe->upage))
	  pagedir_set_accessed (t->pagedir, fe->upage, false);
      else
	{
	  struct spt_directory_element *sde = 
	    spt_directory_find (fe->tid);
	  ASSERT (sde != NULL);
	  struct spt_element *se = spt_find (sde, fe->upage);
	  ASSERT (se != NULL);

	  if (se->writable == false && se->file != NULL)
	    {
	      spt_element_set_page (se, NULL, FRAME_FILE, 0, se->file, 
				    se->file_offset, se->file_read_bytes, 
				    se->file_zero_bytes, se->writable);
	      pagedir_clear_page (t->pagedir, fe->upage);

	      frame_element_set (fe, NULL, TID_ERROR);
	      return fe->kpage;	      
	    }

	  /* evict the frame */
	  uint32_t swap_index = bitmap_scan_and_flip (swap_table, 0, 
						      1, false);

	  if (swap_index == BITMAP_ERROR)
	    return NULL;

	  uint32_t sector_no = swap_index * NUM_SECTORS_IN_PAGE;
	  /* write to swap disk */
	  int i;
	  for (i = 0; i < NUM_SECTORS_IN_PAGE; ++i)
	    block_write (swap_partition, sector_no + i, 
			 (char *) fe->kpage + BLOCK_SECTOR_SIZE * i);

	  /* supp page table update */
	  spt_element_set_page (se, NULL, FRAME_SWAP, swap_index,
				NULL, 0, 0, 0, true);
	  /* page table update */
	  pagedir_clear_page (t->pagedir, se->upage);
	  
	  /* frame table update */
	  frame_element_set (fe, NULL, TID_ERROR);
	  return fe->kpage;
	}
    }
  return NULL;
}

/* Indicate that the swap disk at index SWAP_INDEX is free */
void 
swap_free (uint32_t swap_index)
{
  bitmap_set (swap_table, swap_index, false);
}
