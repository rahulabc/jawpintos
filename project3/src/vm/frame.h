#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include "threads/palloc.h"

void valloc_init (size_t user_page_limit);
void *valloc_get_page (enum palloc_flags);
void *valloc_get_multiple (enum palloc_flags, size_t page_cnt);
void valloc_free_page (void *);
void valloc_free_multiple (void *, size_t page_cnt);

#endif /* vm/page.h */

