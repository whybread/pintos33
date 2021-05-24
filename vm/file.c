/* file.c: Implementation of memory backed file object (mmaped object). */
#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "vm/vm.h"
#include "hash.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

struct lazyload {
	struct file *file;
	uint8_t *upage;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	off_t offset;
	bool writable;
};

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	struct frame *frame = malloc(sizeof(struct frame));
	page->frame = frame;
	frame->page = page;
	struct lazyload *aux = (struct lazyload *) file_page->aux;
	file_read_at (aux->file, kva, aux->read_bytes, aux->offset);
	pml4_set_page(thread_current()->pml4, page->va, kva, aux->writable);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	frame->kva = pml4_get_page(thread_current()->pml4, page->va);
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		struct lazyload *aux = (struct lazyload *) file_page->aux;
		file_write_at(aux->file, page->frame->kva, 0x1000, aux->offset);
	}
	palloc_free_page(page->frame->kva);
	pml4_clear_page(thread_current()->pml4, page->va);
	free(page->frame);
	page->frame = NULL;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	free(page->frame);
	free(page);
}
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	pml4_clear_page (t->pml4, upage);

	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
static bool
lazyload_file (struct page *page, void *aux) {
	struct file_page *file_page UNUSED = &page->file;
	struct lazyload * aux1 = (struct lazyload *) aux;
	file_seek (aux1->file, aux1->offset); 
	uint8_t *kpage = page->frame->kva;
	file_read (aux1->file, kpage, aux1->read_bytes);
	if (!install_page (aux1->upage, kpage, aux1->writable)) {
		palloc_free_page (kpage);
		return false;
	}
	page->frame->kva = pml4_get_page(thread_current()->pml4, page->va);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	file_page->aux = aux;
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *taddr = addr;
	if (addr == 0 || (addr+length) == 0 || is_kernel_vaddr(addr) || is_kernel_vaddr(addr+length)
		|| length == 0 || offset % 0x1000 != 0) return NULL;
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if(page != NULL && VM_TYPE(page -> operations -> type) == VM_ANON) return NULL;
	struct list_elem *e;
	struct mm_file* mm;
	for (e = list_begin (&(thread_current()->mmfile_list)); e != list_end (&(thread_current()->mmfile_list)); e = list_next (e)) {
		mm = list_entry (e, struct mm_file, elem);
		if(mm->addr == addr){
			return NULL; 
		}
	}
	struct mm_file* mfile = malloc(sizeof(struct mm_file));
	mfile->addr = addr;
	list_init (&(mfile->page_list));
	int length_tmp = length;
	off_t offset_check = -1;
	if (offset == 0) offset_check = 0;
	while (length_tmp) {
		void *aux = NULL;
		struct lazyload *aux1 = (struct lazyload *)malloc(sizeof(struct lazyload));
		struct page_addr* paddr = malloc(sizeof(struct page_addr));
		aux1->file = (uint64_t)file;
		aux1->upage = addr;
		paddr->file = file;
		paddr->addr = addr;
		if (length_tmp < 0x1000){
			paddr->size = length_tmp;
			aux1->read_bytes = length_tmp;
			aux1->zero_bytes = 0x1000 - length_tmp;
		}
		else {
			paddr->size = 0x1000;
			aux1->read_bytes = 0x1000;
			aux1->zero_bytes = 0;
		}
		aux1->writable = writable;
		if (offset_check == -1){
			aux1->offset = offset;
			paddr->offset = offset;
		}
		else{
			aux1->offset = offset_check;
			paddr->offset = offset_check;
			offset_check += 0x1000;
		}
		aux = aux1;
		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazyload_file, aux))
			return false;
		length_tmp -= 0x1000;
		addr += 0x1000;
		list_push_back (&(mfile->page_list), &(paddr->elem));
	}
	list_push_back (&(thread_current()->mmfile_list), &(mfile->elem));
	return taddr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct list_elem *e;
	struct mm_file* mm;
	struct mm_file* mm_free = NULL;
	for (e = list_begin (&(thread_current()->mmfile_list)); e != list_end (&(thread_current()->mmfile_list)); e = list_next (e)) {
		mm = list_entry (e, struct mm_file, elem);
		if (mm->addr == addr){
			mm_free = mm;
			break;
		}
	}
	if (mm_free == NULL) return;
	list_remove(e);
	struct page_addr* paddr;
	for (e = list_begin (&(mm_free->page_list)); e != list_end (&(mm_free->page_list));) {
		struct page *page = NULL;
		paddr = list_entry (e, struct page_addr, elem);
		page = spt_find_page(&thread_current()->spt, paddr->addr);
		e = list_next (e);
		if(page != NULL) {
			if (pml4_is_dirty(thread_current()->pml4, page->va)){
				file_write_at(paddr -> file, paddr -> addr, paddr -> size, paddr -> offset);
			}
			hash_delete(thread_current()->spt.hash_table, &(page->elem));
			free(paddr);
		}
	}
	free(mm_free);
	return;
}
