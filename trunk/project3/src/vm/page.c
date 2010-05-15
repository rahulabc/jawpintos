#include "vm/page.h"
#include <stdbool.h>
#include "lib/kernel/hash.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

enum frame_source
  {
    FRAME_ZERO = 0,
    FRAME_FRAME,
    FRAME_SWAP,
    FRAME_FILE
  };

/* supplemental page table */
static struct hash spt; 

struct spt_elem
  {
    struct hash process_spt;
    tid_t tid;
    struct hash_elem elem;
  }; 

struct process_spt_elem
  {
    uint32_t *kpage;
    uint32_t *upage;
    enum frame_source source;
    struct file *file;
    int file_offset;
    struct hash_elem elem;
  };

static unsigned 
spt_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct spt_elem *h = hash_entry (h_, struct spt_elem, elem);
  return hash_int (h->tid);
}

static bool 
spt_less (const struct hash_elem *a_, const struct hash_elem *b_, 
	  void *aux UNUSED)
{
  const struct spt_elem *a = hash_entry (a_, struct spt_elem, elem);
  const struct spt_elem *b = hash_entry (b_, struct spt_elem, elem);
  
  return a->tid < b->tid;
}

static unsigned
process_spt_hash (const struct hash_elem *h_, void *aux UNUSED)
{
  const struct process_spt_elem *h = hash_entry (h_, struct process_spt_elem, elem);
  return hash_bytes (&h->upage, sizeof (h->upage));
}

static bool 
process_spt_less (const struct hash_elem *a_, const struct hash_elem *b_, 
		 void *aux UNUSED)
{
  const struct process_spt_elem *a = hash_entry (a_, struct process_spt_elem, elem);
  const struct process_spt_elem *b = hash_entry (b_, struct process_spt_elem, elem);
  
  return a->upage < b->upage;
}

void 
spt_init (void)
{
  hash_init (&spt, spt_hash, spt_less, NULL);
}

static struct hash *
get_process_spt (tid_t tid)
{
  struct spt_elem tmp;
  tmp.tid = tid;
  struct hash_elem *h = hash_find (&spt, &tmp.elem);

  if (h == NULL)
    {
      struct spt_elem *se = (struct spt_elem *) 
	malloc (sizeof (struct spt_elem));
      ASSERT (se != NULL);
      
      se->tid = tid;
      hash_init (&se->process_spt, process_spt_hash, 
		 process_spt_less, NULL);
      h = hash_insert (&spt, &se->elem);
    }

  struct spt_elem *spt_e = hash_entry (h, struct spt_elem, elem);
  return &spt_e->process_spt;
}

static struct process_spt_elem *
get_process_spt_elem (tid_t tid, uint32_t *upage)
{
  struct hash *process_spt = get_process_spt (tid);

  struct process_spt_elem tmp;
  tmp.upage = upage;
  struct hash_elem *h = hash_find (process_spt, &tmp.elem);

  if (h == NULL)
    {
      struct process_spt_elem *pse = (struct process_spt_elem *) 
	malloc (sizeof (struct process_spt_elem));
      ASSERT (pse != NULL);
      pse->upage = upage;
      h = hash_insert (process_spt, &pse->elem);
    }

  struct process_spt_elem *pspt_e = hash_entry (h, struct process_spt_elem, elem);
  return pspt_e;
}

static const int INVALID_OFFSET = -1;

bool 
spt_pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw)
{
  tid_t tid = thread_current ()->tid;

  struct process_spt_elem *pse = get_process_spt_elem (tid, upage);
  ASSERT (pse->upage == upage);
  pse->kpage = kpage;
  pse->source = FRAME_FRAME;
  pse->file = NULL;
  pse->file_offset = INVALID_OFFSET;
    
  return pagedir_set_page (pd, upage, kpage, rw);
}
