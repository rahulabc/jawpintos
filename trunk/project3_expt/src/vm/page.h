#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include "filesys/file.h"
#include "threads/thread.h"
#include <hash.h>

enum frame_source 
  {
    FRAME_ZERO = 0,
    FRAME_FRAME,
    FRAME_SWAP,
    FRAME_FILE,
    FRAME_INVALID
  };

struct spt_directory_element
  {
    tid_t tid;  /* key */
    struct hash spt;
    struct hash_elem spt_dir_elem;
    struct lock spt_lock;
  };

struct spt_element
  {
    uint32_t *upage;  /* key */
    uint32_t *kpage;
    enum frame_source source;
    uint32_t swap_index;
    struct file *file;
    off_t file_offset;
    int file_read_bytes;
    int file_zero_bytes;
    bool writable;
    struct hash_elem spt_elem;
  };



void spt_directory_init (void);
bool spt_pagedir_update (struct thread *t, uint32_t *upage, 
			 uint32_t *kpage, enum frame_source source,
			 uint32_t swap_index, struct file *file,
			 off_t file_offset, int file_read_bytes,
			 int file_zero_bytes, bool writable);
bool spt_page_exist (struct thread *t, uint32_t *upage);
enum frame_source spt_get_source (tid_t tid, uint32_t *upage);
struct spt_directory_element *spt_directory_find (tid_t tid);
struct spt_element *spt_find (struct spt_directory_element *sde, uint32_t *upage);
void spt_element_set_page (struct spt_element *se, uint32_t *kpage,
			   enum frame_source source, uint32_t swap_index,
			   struct file *file, off_t file_offset, 
			   int file_read_bytes, int file_zero_bytes,
			   bool writable);
void spt_free (tid_t tid);
struct spt_directory_element *spt_directory_element_create (tid_t tid);
struct spt_element *spt_element_create (struct spt_directory_element *sde,
					uint32_t *upage);
void spt_free_mmap (tid_t tid, void *upage);

#endif /* vm/page.h */
