#include "vm/page.h"

bool st_pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw)
{
  pagedir_set_page (pd, upage, kpage, rw);
}


