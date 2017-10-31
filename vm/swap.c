#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "swap.h"

static struct block *swapPartition;

static size_t pageSectors = PGSIZE/BLOCK_SECTOR_SIZE;
static struct bitmap *swapSpace;

void init_swap() {
	swapPartition = block_get_role(BLOCK_SWAP);
	if(swapPartition == NULL) PANIC("uh oh");
	int temp = block_size(swapPartition)/pageSectors;
	swapSpace = bitmap_create(temp);
	if(swapSpace == NULL) PANIC("uh oh");

	bitmap_set_all(swapSpace, false);
}

size_t swapToDisk(const void *virtualAddr){
	size_t swapIndex = bitmap_scan_and_flip(swapPartition, 0, 1, false);
	if(swapIndex == BITMAP_ERROR) return SWAPERROR;

	int i;
	for(i = 0; i < pageSectors; i++){
		block_sector_t sector = (swapIndex * pageSectors) + i;
		block_sector_t buffer = virtualAddr + (i * BLOCK_SECTOR_SIZE);
		block_write(swapPartition, sector, buffer);
	}
	return swapIndex;
}

void swapFromDisk(size_t swapIndex, void *virtualAddr){
	int i;
	for(i = 0; i < pageSectors; i++){
		block_sector_t sector = (swapIndex * pageSectors) + i;
		block_sector_t buffer = virtualAddr + (i * BLOCK_SECTOR_SIZE);
		block_read(swapPartition, sector, buffer);
	}
}

void toggleSwap(size_t swapIndex){
	bitmap_flip(swapSpace, swapIndex);
}

