#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static bool install_page (void *upage, void *kpage, bool writable);
static void add_frame_table (void *upage, void *kpage);

struct frame_elem
  { 
    void *kpage;
    void *upage;
    tid_t tid;
    struct list_elem elem;
  };

static struct list frame_table;

void 
valloc_init (size_t user_page_limit) 
{
  palloc_init (user_page_limit);
}

void 
frame_table_init ()
{
  list_init (&frame_table);
}

void *
valloc_get_page (enum palloc_flags flags, void *upage, bool writable)
{
  ASSERT (upage == pg_round_down(upage));
  void *kpage = palloc_get_page (flags | PAL_ASSERT);
  if (kpage == NULL) 
    return kpage;
  if (install_page(upage, kpage, writable))
    return kpage;
  else 
    valloc_free_page (kpage);
  return NULL;
}

static bool 
install_page (void *upage, void *kpage, bool writable)
{
  ASSERT (upage == pg_round_down(upage));
  struct thread *t = thread_current (); 

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  if ((pagedir_get_page (t->pagedir, upage) == NULL) &&
       pagedir_set_page (t->pagedir, upage, kpage, writable))
    {   
       add_frame_table (upage, kpage);
       return true;
    }   
  return false;
}

static void 
add_frame_table (void *upage, void *kpage)
{
  struct thread *t = thread_current();
  struct frame_elem *fe = (struct frame_elem *) malloc(sizeof (struct frame_elem));
  fe->kpage = kpage;
  fe->upage = upage;
  fe->tid = t->tid;
  list_push_back (&frame_table, &fe->elem);
}

void *valloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  return palloc_get_multiple (flags, page_cnt);
}

void valloc_free_page (void *kpage)
{
  valloc_free_multiple (kpage, 1);
}

void valloc_free_multiple (void *kpages, size_t page_cnt)
{
  // TODO: hash or list?
  struct list_elem *e = list_begin (&frame_table);
  while (e != list_end (&frame_table))
    {
      struct frame_elem *fe = list_entry (e, struct frame_elem, elem);
      if (kpages == fe->kpage)
        {
          list_remove (e);
          free (fe);
          break;
        }
      e = list_next (e);
    }

  palloc_free_multiple (kpages, page_cnt);
}


