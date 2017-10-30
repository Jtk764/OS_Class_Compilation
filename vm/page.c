#include "vm/page.h"
#include "threads/pte.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "string.h"
#include "userprog/syscall.h"
#include "vm/swap.h"



unsigned
suppl_pt_hash (const struct hash_elem *he, void *aux UNUSED)
{
  const struct suppl_pte *vspte;
  vspte = hash_entry (he, struct suppl_pte, elem);
  return hash_bytes (&vspte->uvaddr, sizeof vspte->uvaddr);
}

/* Functionality required by hash table*/
bool
suppl_pt_less (const struct hash_elem *hea,
               const struct hash_elem *heb,
	       void *aux UNUSED)
{
  const struct suppl_pte *vsptea;
  const struct suppl_pte *vspteb;
 
  vsptea = hash_entry (hea, struct suppl_pte, elem);
  vspteb = hash_entry (heb, struct suppl_pte, elem);

  return (vsptea->uvaddr - vspteb->uvaddr) < 0;
}



static bool
load_page_file (struct suppl_pte *spte)
{
  struct thread *cur = thread_current ();
  
  file_seek (spte->data.file_page.file, spte->data.file_page.ofs);

  /* Get a page of memory. */
  uint8_t *kpage = allocate_frame (PAL_USER);
  if (kpage == NULL)
    return false;
  
  /* Load this page. */
  if (file_read (spte->data.file_page.file, kpage,
		 spte->data.file_page.read_bytes)
      
      != (int) spte->data.file_page.read_bytes)
    {
      free_frame (kpage);
      return false; 
    }
  memset (kpage + spte->data.file_page.read_bytes, 0,
	  spte->data.file_page.zero_bytes);
  
  /* Add the page to the process's address space. */
  if (!pagedir_set_page (cur->pagedir, spte->upageaddr, kpage,
			 spte->data.file_page.writable))
    {
      free_frame (kpage);
      return false; 
    }
  
  spte->is_loaded = true;
  return true;
}

static bool
load_page_swap (struct suppl_pte *spte)
{
  /* Get a page of memory. */
  uint8_t *kpage = allocate_frame (PAL_USER);
  if (kpage == NULL)
    return false;
 
  /* Map the user page to given frame */
  if (!pagedir_set_page (thread_current ()->pagedir, spte->upageaddr, kpage, 
			 spte->swap_writable))
    {
      free_frame (kpage);
      return false;
    }
 
  /* Swap data from disk into memory page */
  swapFromDisk (spte->swapIndex, spte->upageaddr);

  if (spte->in_swap)
    {
      /* After swap in, remove the corresponding entry in suppl page table */
      hash_delete (&thread_current ()->suppl_page_table, &spte->elem);
    }
  if (spte->in_swap && spte->is_file)
    {
      spte->in_swap=false;
      spte->is_loaded = true;
    }

  return true;
}

static void
free_suppl_pte (struct hash_elem *e, void *aux UNUSED)
{
  struct suppl_pte *spte;
  spte = hash_entry (e, struct suppl_pte, elem);
  if (spte->in_swap)
    toggleSwap (spte->swapIndex);

  free (spte);
}

bool 
insert_suppl_pte (struct hash *spt, struct suppl_pte *spte)
{
  struct hash_elem *result;

  if (spte == NULL)
    return false;
  
  result = hash_insert (spt, &spte->elem);
  if (result != NULL)
    return false;
  
  return true;
}

bool
suppl_pt_insert_file (struct file *file, off_t ofs, uint8_t *upage, 
		      uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct suppl_pte *spte; 
  struct hash_elem *result;
  struct thread *cur = thread_current ();

  spte = calloc (1, sizeof *spte);
  
  if (spte == NULL)
    return false;
  
  spte->upageaddr = upage;
  spte->data.file_page.file = file;
  spte->data.file_page.ofs = ofs;
  spte->data.file_page.read_bytes = read_bytes;
  spte->data.file_page.zero_bytes = zero_bytes;
  spte->data.file_page.writable = writable;
  spte->is_loaded = false;
      
  result = hash_insert (&cur->suppl_page_table, &spte->elem);
  if (result != NULL)
    return false;

  return true;
}

void grow_stack (void *upageaddr)
{
  void *spage;
  struct thread *t = thread_current ();
  spage = allocate_frame (PAL_USER | PAL_ZERO);
  if (spage == NULL)
    return;
  else
    {
      /* Add the page to the process's address space. */
      if (!pagedir_set_page (t->pagedir, pg_round_down (upageaddr), spage, true))
	{
	  free_frame (spage); 
	}
    }
}

void free_suppl_pt (struct hash *suppl_pt) 
{
  hash_destroy (suppl_pt, free_suppl_pte);
}
