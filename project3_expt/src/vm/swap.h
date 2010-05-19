#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "threads/thread.h"
#include <inttypes.h>

void swap_init (void);
bool swap_fetch (tid_t tid, uint32_t *upage, uint32_t *kpage);
void *swap_evict (void);
void swap_free (uint32_t swap_index);

#endif /* vm/swap.h */
