#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/thread.h"


struct list frame_table;
struct lock frame_lock;



struct frame_element
  {
    uint32_t *kpage;
    uint32_t *upage;
    tid_t tid;
    struct list_elem frame_elem;
  };


void frame_init (void);
void *frame_get_page (enum palloc_flags flags);
bool frame_table_update (tid_t tid, uint32_t *upage, uint32_t *kpage);
void frame_element_set (struct frame_element *fe, uint32_t *upage, tid_t tid);
struct frame_element *frame_element_create (uint32_t *kpage);
struct frame_element *frame_table_find (uint32_t *kpage);
void frame_free_page (uint32_t *kpage);

#endif /* vm/frame.h */
