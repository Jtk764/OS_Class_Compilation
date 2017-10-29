

#define STACK_SIZE (8 * (1 << 20))

#include <stdio.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

struct suppl_pte_data;
  {
    struct file * file;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
} 

struct suppl_pte
{
  void *upageddr;   //user virtual address as the unique identifier of a page
  struct suppl_pte_data data;
  bool is_loaded;

  /* reserved for possible swapping */
  size_t swap_slot_idx;
  bool swap_writable;

  struct hash_elem elem;
};


void vm_page_init(void);
unsigned suppl_pt_hash (const struct hash_elem *, void * UNUSED);
bool suppl_pt_less (const struct hash_elem *, 
		    const struct hash_elem *,
		    void * UNUSED);

bool insert_suppl_pte (struct hash *, struct suppl_pte *);

bool suppl_pt_insert_file ( struct file *, off_t, uint8_t *, 
uint32_t, uint32_t, bool);

void write_page_back_to_file_wo_lock (struct suppl_pte *);
void free_suppl_pt (struct hash *);
bool load_page (struct suppl_pte *);
void grow_stack (void *);