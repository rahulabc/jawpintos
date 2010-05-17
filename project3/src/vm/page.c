#include "vm/page.h"
#include <stdbool.h>
#include "lib/kernel/hash.h"
#include "lib/string.h"
#include "lib/stdio.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "userprog/syscall.h"

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
    off_t file_offset;
    int file_read_bytes;
    int file_zero_bytes;
    int writable;

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
      if (h == NULL)
        h = &se->elem;
    }

  struct spt_elem *spt_e = hash_entry (h, struct spt_elem, elem);
  return &spt_e->process_spt;
}

static struct process_spt_elem *
get_process_spt_elem (tid_t tid, uint32_t *upage)
{
  ASSERT (pg_ofs (upage) == 0);
  struct hash *process_spt = get_process_spt (tid);

  struct process_spt_elem tmp;
  tmp.upage = upage;
  struct hash_elem *h = hash_find (process_spt, &tmp.elem);

  if (h == NULL)
    {
      struct process_spt_elem *pse = (struct process_spt_elem *) 
      malloc (sizeof (struct process_spt_elem));
      ASSERT (pse != NULL);
      pse->source = FRAME_FRAME;
      pse->upage = upage;
      h = hash_insert (process_spt, &pse->elem);
      if (h == NULL)
        h = &pse->elem;
    }

  struct process_spt_elem *pspt_e = hash_entry (h, struct process_spt_elem, elem);
  return pspt_e;
}

static const int INVALID_OFFSET = -1;

bool 
spt_pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw)
{
  ASSERT (pg_ofs (upage) == 0);
  tid_t tid = thread_current ()->tid;

  struct process_spt_elem *pse = get_process_spt_elem (tid, upage);
  ASSERT (pse->upage == upage);
  pse->kpage = kpage;
  pse->source = FRAME_FRAME;
  pse->file = NULL;
  pse->file_offset = 0;
  pse->file_read_bytes = 0;
  pse->file_zero_bytes = 0;
    
  return pagedir_set_page (pd, upage, kpage, rw);
}

void 
spt_update_file(void *upage, struct file *file, off_t ofs,
                size_t page_read_bytes, size_t page_zero_bytes,
                bool writable)
{
  tid_t tid = thread_current ()->tid;

  struct process_spt_elem *pse = get_process_spt_elem (tid, upage);
  ASSERT (pse->upage == upage);
  pse->kpage = NULL;
  pse->source = FRAME_FILE;
  pse->file = file;
  pse->file_offset = ofs;
  pse->file_read_bytes = page_read_bytes;
  pse->file_zero_bytes = page_zero_bytes;
  pse->writable = writable;
  return;  
}

bool spt_load_page(void *fault_addr, struct intr_frame *f, bool user)
{
  void *upage = pg_round_down (fault_addr);
  /* If in user mode, the fault address should always be 
     in the user memory in the range of (0, PHYS_BASE) */
  if (user)
    {   
      if (!is_user_vaddr (fault_addr))
        {
          syscall_thread_exit (f, -1);
          return false;
        }
    }   

  /* stack allocation: 
     if in user mode, use the interrupt frame's stack pointer,
     if in kernel mode, use the saved user program's stack pointer in
     the current thread */
  struct thread *t = thread_current();

  if (is_user_vaddr(fault_addr) && ((uint32_t)fault_addr >=  
    (user ? (uint32_t) f->esp: (uint32_t)t->user_esp) - 32))
    {  
      /* stack growth, or lazy access of stack */ 
      valloc_get_page (PAL_USER, upage, true);
      return true;
    } 
  if (! user)  // REVISIT?
    {
      if (!is_user_vaddr (fault_addr))  // if kernel address
        { 
          // allow kernel to access any kernel section
          valloc_get_page (0, upage, true);
          return true;
        }
    } 
      
  struct process_spt_elem *pse = get_process_spt_elem (t->tid, upage);
  if (pse->source == FRAME_FILE)  
    {
      uint8_t *kpage = valloc_get_page (user?PAL_USER:0, upage, pse->writable);
      if (kpage == NULL) 
        {
          return false;
        }
      file_seek (pse->file, pse->file_offset);
      if (file_read (pse->file, kpage, pse->file_read_bytes) != 
                  (int) pse->file_read_bytes)
        {
          valloc_free_page (kpage);
          return false;
        } 
      memset (kpage + pse->file_read_bytes, 0, pse->file_zero_bytes);
      return true;
    }
  
  if (pse->source == FRAME_ZERO) // REVISIT
    {
      uint8_t *kpage = valloc_get_page (user?PAL_USER:0, upage, pse->writable);
      if (kpage == NULL) 
        {
          return false;
        }
      memset (kpage, 0, PGSIZE);
      return true;
    }
  return false;
}

