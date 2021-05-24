/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

bool compare_hash (const struct hash_elem *a, const struct hash_elem *b, void *aux);
unsigned apply_hash (const struct hash_elem *e, void *aux);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
 
const void* stack_limit = (void *) (((uint8_t *) USER_STACK) - 256*PGSIZE);

void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc(sizeof(struct page));
		if(VM_TYPE(type) == VM_ANON) uninit_new(page, upage, init, type, aux, anon_initializer);
		else if(VM_TYPE(type) == VM_FILE) uninit_new(page, upage, init, type, aux, file_backed_initializer);

		/* TODO: Insert the page into the spt. */
		page->writable = writable;
		page->evict_cnt = 0;
		if(spt_insert_page(spt, page)) printf("vm_alloc_page error\n");
	}
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	struct page p;
	struct hash_elem *e;
	p.va = (uint64_t) va / 0x1000 * 0x1000;
	e = hash_find (spt->hash_table, &p.elem);
	return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	/* TODO: Fill this function. */
	return hash_insert (spt->hash_table, &page->elem);
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	 /* TODO: The policy for eviction is up to you. */
	struct page* p;
	struct hash_iterator i;
	struct list *list_vic = &thread_current()->spt.list_vic;
	struct vic_elem* vic;

	while (list_size(list_vic)){
		vic = list_entry (list_pop_front (list_vic), struct vic_elem, elem);
		p = spt_find_page (&thread_current()->spt, vic->va);

		if(p->frame == NULL || pg_ofs(p->frame->kva)){
			list_push_back(list_vic, &vic->elem);
			continue;
		}
		p->evict_cnt ++;
		if(p->evict_cnt == 5){
			free(vic);
			continue;
		}
		break;
	}
	swap_out (p);
	return palloc_get_page(PAL_USER|PAL_ZERO);
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (struct page* page) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */	
	frame = palloc_get_page(PAL_USER | PAL_ZERO);
	if(frame == NULL) frame = vm_evict_frame();

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	
	struct vic_elem* vic = malloc(sizeof(struct vic_elem));
	vic->va = page->va;

	if (VM_TYPE(page->operations->type) == VM_UNINIT || VM_TYPE(page->operations->type) == VM_FILE)
		list_push_back(&thread_current()->spt.list_vic, &(vic->elem));
	else
		list_push_front(&thread_current()->spt.list_vic, &(vic->elem));
	page->frame->shared = 1;
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	//still implementing
	struct page *kpage;
	struct frame *frame;
	uint8_t *kva;
	bool succ;
	uint64_t ad = (uint64_t) addr / 0x1000 * 0x1000;
	struct supplemental_page_table *spt = &thread_current()->spt;
	while (spt->stack_bottom > ad){
		spt->stack_bottom -= 0x1000;
		kpage = malloc(sizeof(struct page));
		frame = malloc(sizeof(struct frame));
		frame->page = kpage;
		kpage->frame = frame;
		kpage->writable = true;
		kpage->va = spt->stack_bottom;
		kva = vm_get_frame (kpage);
		if (kva != NULL) {
			if (pml4_set_page (thread_current()->pml4, spt->stack_bottom, kva, true)) {
				frame->kva = pml4_get_page(thread_current()->pml4, kpage->va);
			}
			else palloc_free_page (frame);
		}
		kpage->frame->kva = kva;
		anon_initializer (kpage, VM_ANON + VM_MARKER_0, NULL);
		spt_insert_page(spt, kpage);
		kpage->is_stack = true;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(!is_user_vaddr(addr)) process_exit();
	page = spt_find_page(spt, addr);
	if (page == 0) {
		if (addr >= stack_limit && addr < spt->stack_bottom && (f->rsp) != (f->R.rbp)) {
			vm_stack_growth (addr);
			return true;
		}
		else process_exit();
	}
	if (page->frame != NULL) {
		if (page->writable && page->frame->shared > 1) {
			struct frame *frame = malloc(sizeof(struct frame));
			uint8_t *kpage = vm_get_frame(page);
			memcpy (kpage, page->frame->kva, 0x1000);
			frame->page = page;
			page->frame = frame;
			pml4_clear_page (thread_current()->pml4, page->va);
			pml4_set_page (thread_current()->pml4, page->va, kpage, true);
			frame->kva = pml4_get_page (thread_current()->pml4, page->va);
			return true;
		}
		else process_exit();
	}
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	return vm_do_claim_page (spt_find_page (&thread_current()->spt, va));
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = malloc(sizeof(struct frame));
	frame->page = page;
	page->frame = frame;
	uint8_t *kpage = vm_get_frame(page);
	frame->kva = kpage;
	/* Set links */

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	return swap_in (page, frame->kva);
}

bool 
compare_hash (const struct hash_elem *a, const struct hash_elem *b, void *aux){
	const struct page *page1 = hash_entry(a, struct page, elem);
	const struct page *page2 = hash_entry(b, struct page, elem);
	return page1 -> va < page2 -> va;
}

unsigned
apply_hash (const struct hash_elem *e, void *aux){
	const struct page *page1 = hash_entry(e, struct page, elem);
	return hash_bytes(&page1 -> va, sizeof(page1 -> va));
}

void
free_hash (struct hash_elem *e, void *aux){
	struct page *page1 = hash_entry(e, struct page, elem);
	if(page1->frame != NULL && page1->frame->shared>1){
		page1->frame->shared --;
		pml4_clear_page(thread_current()->pml4, page1->va);
		free(page1);
	}
	else
		destroy(page1);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	struct hash *hash = malloc(sizeof(struct hash));
	hash_init(hash, apply_hash, compare_hash, NULL);
	list_init(&spt->list_vic);
	spt->hash_table = hash;
	spt->stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash *dhash = dst->hash_table;
	struct hash_iterator iter;
   	hash_first (&iter, src->hash_table);
   	while (hash_next (&iter)) {
		struct page* dpage = malloc(sizeof(struct page));
  		struct page* spage = hash_entry (hash_cur (&iter), struct page, elem);
		memcpy(dpage, spage, sizeof(struct page));
		if(spage->frame != NULL) {
			struct frame *frame = spage->frame;
			if (spage->is_stack) {
				struct frame *frame = malloc(sizeof(struct frame));
				frame->page = dpage;
				dpage->frame = frame;
				uint8_t *kpage = vm_get_frame (dpage);
				memcpy(kpage, spage->frame->kva, 0x1000);
				pml4_clear_page (thread_current()->pml4, dpage->va);
				pml4_set_page (thread_current()->pml4, dpage->va, kpage, true);
				frame->kva = pml4_get_page (thread_current()->pml4, dpage->va);
			}
			dpage->frame = frame;
			pml4_clear_page (thread_current()->pml4, dpage->va);
			pml4_set_page (thread_current()->pml4, dpage->va, frame->kva, false);
			spage->frame->shared++;
			dpage->frame->shared = spage->frame->shared;
		}
		hash_insert(dhash, &(dpage->elem));
   	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	if(spt->hash_table == NULL || hash_size(spt->hash_table) == 0) return;
	hash_destroy (spt -> hash_table, free_hash);
}
