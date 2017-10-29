#include "threads/thread.h"

struct frame {
  void *frame;
  tid_t tid;
  uint32_t *pte;
  void *upage;
  struct list_elem elem;
};

struct list frames;

/* frame allocation functionalitie */
void frame_init (void);
void *allocate_frame (enum palloc_flags flags);
void free_frame (void *);

/* frame table management functionalities */
void assign_frame (void*, uint32_t *, void *);

/* evict a frame to be freed and write the content to swap slot or file*/
void *evict_frame (void);