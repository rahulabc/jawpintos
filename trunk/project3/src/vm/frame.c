#include "vm/frame.h"
#include "threads/palloc.h"

void valloc_init (size_t user_page_limit) 
{
  palloc_init (user_page_limit);
}

void *valloc_get_page (enum palloc_flags flags)
{
  void *page = palloc_get_page (flags | PAL_ASSERT);
  if (page == NULL) 
    return page;
  // populate page table
  // populate this frame in the frame table
  // return the virtual address 
  return page;
}

void *valloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  return palloc_get_multiple (flags, page_cnt);
}

void valloc_free_page (void *page)
{
  palloc_free_page (page);
}

void valloc_free_multiple (void *pages, size_t page_cnt)
{
  palloc_free_multiple (pages, page_cnt);
}


