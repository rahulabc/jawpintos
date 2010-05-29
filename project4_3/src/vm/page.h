#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include "filesys/file.h"

struct file;
struct intr_frame;

void spt_init (void);
bool spt_pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);
void spt_update_file (void *upage, struct file *file, off_t ofs, 
                      size_t page_read_bytes, size_t page_zero_bytes,
                      bool writable);
bool spt_load_page (void *fault_addr, struct intr_frame *f, bool user);

#endif /* vm/page.h */

