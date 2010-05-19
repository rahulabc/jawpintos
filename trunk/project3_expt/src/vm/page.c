#include "vm/page.h"
#include <hash.h>
#include <stdbool.h>
#include <inttypes.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

#include <stdio.h> /*REMOVEME:*/

static struct hash spt_directory; /* supplemental page table */
static struct lock spt_dir_lock;

static unsigned
spt_directory_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct spt_directory_element *h = 
    hash_entry (h_, struct spt_directory_element, spt_dir_elem);
  return hash_int (h->tid);
}

static bool
spt_directory_less (const struct hash_elem *a_, const struct hash_elem *b_, 
		    void *aux UNUSED)
{
  const struct spt_directory_element *a = 
    hash_entry (a_, struct spt_directory_element, spt_dir_elem);
  const struct spt_directory_element *b = 
    hash_entry (b_, struct spt_directory_element, spt_dir_elem);
  return a->tid < b->tid;
}

static unsigned
spt_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct spt_element *h = hash_entry (h_, struct spt_element, spt_elem);
  return hash_bytes (&h->upage, sizeof (h->upage));
}

static bool
spt_less (const struct hash_elem *a_, const struct hash_elem *b_, 
	  void *aux UNUSED)
{
  const struct spt_element *a = hash_entry (a_, struct spt_element, spt_elem);
  const struct spt_element *b = hash_entry (b_, struct spt_element, spt_elem);
  return a->upage < b->upage;
}

void
spt_directory_init (void)
{
  hash_init (&spt_directory, spt_directory_hash, 
	     spt_directory_less, NULL);
  lock_init (&spt_dir_lock);
}

struct spt_directory_element *
spt_directory_find (tid_t tid)
{
  struct spt_directory_element tmp;
  tmp.tid = tid;

  lock_acquire (&spt_dir_lock);
  struct hash_elem *h = hash_find (&spt_directory, &tmp.spt_dir_elem);
  if (h == NULL)
    {
      lock_release (&spt_dir_lock);
      return NULL;
    }
  struct spt_directory_element *spe = 
    hash_entry (h, struct spt_directory_element, 
		spt_dir_elem);
  lock_release (&spt_dir_lock);
  return spe;
}

struct spt_directory_element *
spt_directory_element_create (tid_t tid)
{
  struct spt_directory_element *sde = (struct spt_directory_element *)
    malloc (sizeof (struct spt_directory_element));
  if (sde == NULL)
    return NULL; /* TODO: NEED TO HANDLE THIS MORE CAREFULLY */
  
  sde->tid = tid;
  hash_init (&sde->spt, spt_hash, spt_less, NULL);
  lock_init (&sde->spt_lock);
  lock_acquire (&spt_dir_lock);
  hash_insert (&spt_directory, &sde->spt_dir_elem);
  lock_release (&spt_dir_lock);
  return sde;
}

struct spt_element *
spt_find (struct spt_directory_element *sde, uint32_t *upage)
{
  struct spt_element tmp;
  tmp.upage = upage;

  lock_acquire (&sde->spt_lock);
  struct hash_elem *h = hash_find (&sde->spt, &tmp.spt_elem);
  if (h == NULL)
    {
      lock_release (&sde->spt_lock);
      return NULL;
    }
  
  struct spt_element *se = hash_entry (h, struct spt_element, spt_elem);
  lock_release (&sde->spt_lock);
  return se;
}

void 
spt_element_set_page (struct spt_element *se, uint32_t *kpage,
		      enum frame_source source, uint32_t swap_index,
		      struct file *file, off_t file_offset, 
		      int file_read_bytes, int file_zero_bytes,
		      bool writable)
{
  se->kpage = kpage;
  se->source = source;
  se->swap_index = swap_index;
  se->file = file;
  se->file_offset = file_offset;
  se->file_read_bytes = file_read_bytes;
  se->file_zero_bytes = file_zero_bytes;
  se->writable = writable;  
}

struct spt_element *
spt_element_create (struct spt_directory_element *sde, uint32_t *upage)
{
  struct spt_element *se = (struct spt_element *)
    malloc (sizeof (struct spt_element));
  if (se == NULL)
    return NULL; /* TODO: NEED TO HANDLE THIS MORE CAREFULLY */

  se->upage = upage;  
  spt_element_set_page (se, NULL, FRAME_INVALID, 0, NULL,
			0, 0, 0, false);
  lock_acquire (&sde->spt_lock);
  hash_insert (&sde->spt, &se->spt_elem);
  lock_release (&sde->spt_lock);
  return se;
}

bool
spt_pagedir_update (struct thread *t, uint32_t *upage, 
		    uint32_t *kpage, enum frame_source source,
		    uint32_t swap_index, struct file *file,
		    off_t file_offset, int file_read_bytes,
		    int file_zero_bytes, bool writable)
{
  struct spt_directory_element *sde = 
    spt_directory_find (t->tid);
  if (sde == NULL)
    {
      sde = spt_directory_element_create (t->tid);
      if (sde == NULL)
	return false;
    }

  struct spt_element *se = spt_find (sde, upage);
  if (se == NULL)
    {
      se = spt_element_create (sde, upage);
      if (se == NULL)
	return false;
    }
  spt_element_set_page (se, kpage, source, swap_index, file, file_offset,
			file_read_bytes, file_zero_bytes, writable);
  if (source == FRAME_FILE)
    return true;
  return pagedir_set_page (t->pagedir, upage, kpage, writable);
}

bool
spt_page_exist (struct thread *t, uint32_t *upage)
{
  struct spt_directory_element *sde = 
    spt_directory_find (t->tid);
  if (sde == NULL)
    return false;
  
  struct spt_element *se = spt_find (sde, upage);
  if (se == NULL)
    return false;
  return true;
}

enum frame_source
spt_get_source (tid_t tid, uint32_t *upage)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);
  if (sde == NULL)
    return FRAME_INVALID;
  struct spt_element *se = spt_find (sde, upage);
  if (se == NULL)
    return FRAME_INVALID;
  return se->source;
}

void
spt_free (tid_t tid)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);
  lock_acquire (&spt_dir_lock);

  if (sde != NULL)
    {
      lock_acquire (&sde->spt_lock);
        
      struct hash *spt = &sde->spt;
      struct hash_iterator i;
      hash_first (&i, spt);
      while (hash_next (&i))
	{
	  struct spt_element *se = hash_entry (hash_cur (&i),
					       struct spt_element,
					       spt_elem);
	  if (se->source == FRAME_FRAME /*|| se->source == FRAME_ZERO */) 
	    {
	      pagedir_clear_page (get_thread (tid)->pagedir, se->upage);
	      frame_free_page (se->kpage);
	    }
	  else if (se->source == FRAME_SWAP)
	    swap_free (se->swap_index);
	  
	  hash_delete (spt, &se->spt_elem);
	  free (se);
	  hash_first (&i, spt);
	}
      lock_release (&sde->spt_lock);
      hash_delete (&spt_directory, &sde->spt_dir_elem);
      free (sde);
    }
  lock_release (&spt_dir_lock);
}
