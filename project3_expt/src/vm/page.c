#include "vm/page.h"
#include <hash.h>
#include <stdbool.h>
#include <inttypes.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"


/* set of supplemental page tables for each process */
static struct hash spt_directory; 

/* Hash function used for finding the Supp Page Table
   for a specific process */
static unsigned
spt_directory_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct spt_directory_element *h = 
    hash_entry (h_, struct spt_directory_element, spt_dir_elem);
  return hash_int (h->tid);
}

/* Comparison function used for the hash table of the set of supp page
   tables, SPT_DIRECTORY */
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

/* Hash function used for finding a Supp Page Table 
   entry for a known process */
static unsigned
spt_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct spt_element *h = hash_entry (h_, struct spt_element, spt_elem);
  return hash_bytes (&h->upage, sizeof (h->upage));
}

/* Comparison function used for the hash table of the Supp Page Table 
   entries, SPT */
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

/* Returns the pointer to SPT_DIRECTORY_ELEMENT that is 
   associated with a process specified by the tid.
   Returns NULL if does not exist. */
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
    hash_entry (h, struct spt_directory_element, spt_dir_elem);
  lock_release (&spt_dir_lock);
  return spe;
}

/* Creates a SPT_DIRECTORY ELEMENT specified by a TID
   and returns a pointer to it */
struct spt_directory_element *
spt_directory_element_create (tid_t tid)
{
  struct spt_directory_element *sde = (struct spt_directory_element *)
    malloc (sizeof (struct spt_directory_element));
  if (sde == NULL)
    return NULL;
  
  sde->tid = tid;
  hash_init (&sde->spt, spt_hash, spt_less, NULL);

  lock_acquire (&spt_dir_lock);
  hash_insert (&spt_directory, &sde->spt_dir_elem);
  lock_release (&spt_dir_lock);
  return sde;
}

/* Finds the SPT_ELEMENT from the Supp Page Table specified SDE
   and returns the pointer to it */
struct spt_element *
spt_find (struct spt_directory_element *sde, uint32_t *upage)
{
  struct spt_element tmp;
  tmp.upage = upage;

  lock_acquire (&spt_dir_lock);
  struct hash_elem *h = hash_find (&sde->spt, &tmp.spt_elem);
  if (h == NULL)
    {
      lock_release (&spt_dir_lock);
      return NULL;
    }
  
  struct spt_element *se = hash_entry (h, struct spt_element, spt_elem);
  lock_release (&spt_dir_lock);
  spt_elem_lock_acquire (&se->spt_elem_lock);
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

/* Creates a SPT_ELEMENT inside the Supp Page Table specified by SDE
   and returns a pointer to it */
struct spt_element *
spt_element_create (struct spt_directory_element *sde, uint32_t *upage)
{
  struct spt_element *se = (struct spt_element *)
    malloc (sizeof (struct spt_element));
  if (se == NULL)
    return NULL; 

  se->upage = upage;  
  spt_element_set_page (se, NULL, FRAME_INVALID, 0, NULL,
			0, 0, 0, false);

  hash_insert (&sde->spt, &se->spt_elem);
  lock_init (&se->spt_elem_lock);
  spt_elem_lock_acquire (&se->spt_elem_lock);
  return se;
}


/* Updates the SPT_ELEMENT as specified by the argument.
   Creates an SPT_ELEMENT if it does not exist in Supp Page Table */
bool
spt_pagedir_update (struct thread *t, uint32_t *upage, 
		    uint32_t *kpage, enum frame_source source,
		    uint32_t swap_index, struct file *file,
		    off_t file_offset, int file_read_bytes,
		    int file_zero_bytes, bool writable)
{
  struct spt_element *se = spt_find_or_create (t->tid, upage);
  spt_element_set_page (se, kpage, source, swap_index, file, file_offset,
			file_read_bytes, file_zero_bytes, writable);

  if (source == FRAME_FILE)
    return true;
  return pagedir_set_page (t->pagedir, upage, kpage, writable);
}

/* Finds an SPT_ELEMENT and creates one if it does not exist */
struct spt_element *
spt_find_or_create (tid_t tid, void *upage)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);
  if (sde == NULL)
    {
      sde = spt_directory_element_create (tid);
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
  return se;
}

/* Checks if UPAGE exists in Supp Page Table of process (thread) T */
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

/* Gets the source of the SPT_ELEMENT specified by TID and UPAGE */
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

/* Given an mmappd region's UPAGE, writes it to disk if dirty and
   then clears the pagedir and frees the frame page */
void
spt_free_mmap (tid_t tid, void *upage)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);
  if (sde == NULL)
    return;
  struct spt_element *se = spt_find (sde, upage);
  if (se == NULL)
    return;
  struct thread *t = get_thread (tid);
  if (se->writable)
    {   
      if (pagedir_is_dirty (t->pagedir, upage) && (se->kpage!=NULL)) 
        {   
          file_write_at (se->file, se->kpage, PGSIZE, se->file_offset); 
        }   
    }   

  pagedir_clear_page (t->pagedir, se->upage);
  frame_free_page (se->kpage);

  spt_elem_lock_release (&se->spt_elem_lock);
  return;
}

/* Frees the SPT of a process TID */
void
spt_free (tid_t tid)
{
  struct spt_directory_element *sde = 
    spt_directory_find (tid);

  if (sde != NULL)
    {
      struct hash *spt = &sde->spt;
      struct hash_iterator i;
      hash_first (&i, spt);
      while (hash_next (&i))
	{
	  struct spt_element *se = 
	    hash_entry (hash_cur (&i), struct spt_element, spt_elem);

	  spt_elem_lock_acquire (&se->spt_elem_lock);
	  if (se->source == FRAME_FRAME) 
	    {
	      pagedir_clear_page (get_thread (tid)->pagedir, se->upage);
	      frame_free_page (se->kpage);
	    }
	  else if (se->source == FRAME_SWAP)
	    swap_free (se->swap_index);
	  
	  hash_delete (spt, &se->spt_elem);
	  spt_elem_lock_release (&se->spt_elem_lock);
	  free (se);
	  hash_first (&i, spt);	  
	}
      hash_delete (&spt_directory, &sde->spt_dir_elem);
      free (sde);
    }
}

/* Acquires lock for an SPT_ELEM */
void
spt_elem_lock_acquire (struct lock *lock)
{
  struct thread *t = thread_current ();
  if (t->spt_elem_lock == NULL)
    {
      lock_acquire (lock);
      t->spt_elem_lock = lock;
    }
}

/* Releases lock for an SPT_ELEM */
void
spt_elem_lock_release (struct lock *lock)
{
  struct thread *t = thread_current ();
  lock_release (lock);
  t->spt_elem_lock = NULL;
}
