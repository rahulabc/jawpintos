#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

void spt_init (void);
bool spt_pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);

#endif /* vm/page.h */

