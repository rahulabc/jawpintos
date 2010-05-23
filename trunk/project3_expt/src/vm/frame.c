#include "vm/frame.h"
#include <inttypes.h>
#include <stdbool.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include <string.h>


static struct lock frame_lock;

void 
frame_init (void) 
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

/* Allocates and returns a page.
   First tries palloc, if fails, evicts a frame 
   and returns the newly available frame */
void *
frame_get_page (enum palloc_flags flags)
{
  lock_acquire (&frame_lock);
  void *kpage = palloc_get_page (flags);
  if (kpage == NULL)
    kpage = swap_evict ();
  lock_release (&frame_lock);
  return kpage;
}

/* Frees a frame from the physical memory */
void 
frame_free_page (uint32_t *kpage)
{
  struct frame_element *fe = frame_table_find (kpage);

  /* if the frame about to be evicted is pointed at by
     the clock hand, move the clock hand to the next
     frame */
  if (&fe->frame_elem == clock_hand)
      clock_hand = list_next (clock_hand);

  if (fe != NULL)
    {
      list_remove (&fe->frame_elem);
      free (fe);
      palloc_free_page (kpage);
    }
}

/* Finds and returns a frame given KPAGE */
struct frame_element *
frame_table_find (uint32_t *kpage)
{
  struct list_elem *e;
  struct frame_element *fe = NULL;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fe = list_entry (e, struct frame_element, frame_elem);
      if (fe == NULL)
	break;
      else if (fe->kpage == kpage)
	break;
      else 
	fe = NULL;
    }
  return fe;
}

/* Creates and initializes a frame element, then
   inserts into frame_list */
struct frame_element *
frame_element_create (uint32_t *kpage)
{
  struct frame_element *fe = (struct frame_element *) 
    malloc (sizeof (struct frame_element));
  if (fe == NULL)
    return NULL;
  fe->kpage = kpage;
  fe->upage = NULL;
  fe->tid = TID_ERROR;

  list_push_back (&frame_table, &fe->frame_elem);
  return fe;
}

void
frame_element_set (struct frame_element *fe, uint32_t *upage, tid_t tid)
{
  fe->upage = upage;
  fe->tid = tid;
}

/* Updates a FRAME_ELEMENT. Creates an element if it does not 
   exist in the FRAME_LIST */
bool
frame_table_update (tid_t tid, uint32_t *upage, uint32_t *kpage)
{
  struct frame_element *fe = frame_table_find (kpage);
  if (fe == NULL)
    {
      fe = frame_element_create (kpage);
      if (fe == NULL)
	return false;
    }
  frame_element_set (fe, upage, tid);
  return true;
}
