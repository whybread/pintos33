/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

int disk_cnt = 0;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;	
	struct anon_page *anon_page = &page->anon;
	anon_page->disk_n = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = malloc(sizeof(struct frame));
	page->frame = frame;
	frame->page = page;
	for(int i = 0; i < 8; i++) {
		disk_read (swap_disk, (anon_page->disk_n) * 8 + i, (void *)kva + DISK_SECTOR_SIZE * i);
	}
	pml4_set_page (thread_current()->pml4, page->va, kva, true);
	frame->kva = pml4_get_page (thread_current()->pml4, page->va);
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (anon_page->disk_n == -1) {
		anon_page->disk_n = disk_cnt++;
		for (int i = 0; i < 8; i++) {
			disk_write(swap_disk, (anon_page->disk_n) * 8 + i, (void *) page->frame->kva + DISK_SECTOR_SIZE * i);
		}
	}
	else if (pml4_is_dirty(thread_current()->pml4, page->va)){
		for (int i = 0; i < 8; i++){
			disk_write(swap_disk, (anon_page->disk_n) * 8 + i, (void *) page->frame->kva + DISK_SECTOR_SIZE * i);
		}
	}
	palloc_free_page (page->frame->kva);
	pml4_clear_page (thread_current()->pml4, page->va);
	free (page->frame);
	page->frame = NULL;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	free (page->frame);
	free (page);
}
