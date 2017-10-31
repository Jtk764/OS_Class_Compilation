#ifndef VM_SWAP_H
#define VM_SWAP_H

#define SWAPERROR SIZE_MAX
/*init function*/
void init_swap();

/*swap a frame into swap partition*/
size_t swapToDisk(const void *);

/*swap a frame out of swap partition*/
void swapFromDisk(size_t, void *);

void toggleSwap(size_t);

#endif /* vm/swap.h */