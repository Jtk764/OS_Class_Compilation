#ifndef VM_PAGE_H
#define VM_PAGE_H

#define STACK_SIZE (8 * (1 << 20))

#include <stdio.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include <stdbool.h>

struct suppl_pte_data
  {
    struct file * file; //file
    off_t ofs;			// offset in file
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;		 //if writable
}; 

struct suppl_pte
{
  void *upageaddr;   //user virtual address as the unique identifier of a page
  struct suppl_pte_data data;
  bool is_loaded;
  bool in_swap;
  bool is_file;
  /* reserved for possible swapping */
  size_t swapIndex;
  bool swap_writable;

  struct hash_elem elem;
};


unsigned suppl_pt_hash (const struct hash_elem *, void * UNUSED);
bool suppl_pt_less (const struct hash_elem *, 
		    const struct hash_elem *,
		    void * UNUSED);
struct suppl_pte *get_suppl_pte (struct hash *, void *);
bool insert_suppl_pte (struct hash *, struct suppl_pte *);

bool suppl_pt_insert_file ( struct file *, off_t, uint8_t *, 
uint32_t, uint32_t, bool);

void write_page_back_to_file_wo_lock (struct suppl_pte *);
void free_suppl_pt (struct hash *);
bool load_page (struct suppl_pte *);
void grow_stack (void *);


#endif /* vm/page.h */