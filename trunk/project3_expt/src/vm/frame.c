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


void 
frame_init (void) 
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

void *
frame_get_page (enum palloc_flags flags)
{
  void *kpage = palloc_get_page (flags);
  if (kpage == NULL)
    kpage = swap_evict ();
  return kpage;
}

void 
frame_free_page (uint32_t *kpage)
{
  struct frame_element *fe = frame_table_find (kpage);

  lock_acquire (&frame_lock);

  if (fe != NULL)
    {
      list_remove (&fe->frame_elem);
      free (fe);
      palloc_free_page (kpage);
    }
  lock_release (&frame_lock);
}

struct frame_element *
frame_table_find (uint32_t *kpage)
{
  lock_acquire (&frame_lock);
  struct list_elem *e;
  struct frame_element *fe = NULL;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_element *tmp = list_entry (e, struct frame_element,
					      frame_elem);
      if (tmp == NULL || tmp->kpage == kpage)
	{ 
	  fe = tmp;
	  break;
	}
    }
  lock_release (&frame_lock);
  return fe;
}

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
  lock_acquire (&frame_lock);
  list_push_back (&frame_table, &fe->frame_elem);
  lock_release (&frame_lock);
  return fe;
}

void
frame_element_set (struct frame_element *fe, uint32_t *upage, tid_t tid)
{
  lock_acquire (&frame_lock);
  fe->upage = upage;
  fe->tid = tid;
  lock_release (&frame_lock);
}

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
