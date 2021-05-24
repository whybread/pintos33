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
#ifdef VM
#include "vm/vm.h"
#include "vm/file.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *aux, struct intr_frame *pf);

int put_arg(struct intr_frame *_if, char *save_ptr, uint64_t *argadd) ;
bool func_pte(uint64_t *pte, void *va,  void *aux);
bool is_fork(thread_func *function);
static bool put_page (uint64_t *pte, void *va, void *aux);
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}


/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
	vm_anon_init();
#endif
	int i;
	for(i=0; i<sizeof (thread_current()->name); i++){
		if(thread_current()->name[i]==' '){
			thread_current()->name[i] = '\0';
			break;
		}
	}

	struct child_thread* tchild = malloc(sizeof(struct child_thread));
	tchild -> child = thread_current();
	tchild -> tid = thread_current() -> tid;
	list_push_back(&(thread_current()->parent->child_list), &(tchild->elem));
	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	struct semaphore sema;
	struct intr_frame f;
	enum intr_level old_level = intr_disable();
	memcpy (&f, if_, sizeof (struct intr_frame));
	thread_current()->cf = &f;
	tid_t temp = thread_create (name, PRI_DEFAULT, __do_fork, thread_current());
	if(temp>0){
		thread_current()->fork = temp;
		sema_init(&sema, 0);
		thread_current()->fork_sema = &sema;
		sema_down(&sema);
	}
	intr_set_level(old_level);
	return thread_current()->fork;
}
static int counterr = 0;
bool
func_pte(uint64_t *pte, void *va,  void *aux){
	if (counterr <10){
		counterr++;
	}
	return true;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	void *temp;
	bool writable;
	if(is_kern_pte(pte)){
		return true;
	}
	/* 1. TODO: If the parent_page is kernel page, then return immediately. */

	/* 2. Resolve VA from the parent's page map level 4. */
	printf("%llx \n", va);
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	newpage = palloc_get_page(PAL_USER);
	if(newpage == NULL)
		return false;
	uint64_t *tt = (uint64_t) parent_page;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, 0x1000);
	uint64_t *wc = pml4e_walk (parent->pml4, (uint64_t) va, 0);
	if(2 == is_writable(wc)){
		writable = true;
	}
	else
		writable = false;
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux, struct intr_frame *pf) {           //자식에서 하는거! 뭔 인자 하나를 더 받으라는데 위에 주석이 뭔 쌉소린지 모르겠음
	enum intr_level old_level = intr_disable();
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	parent_if = parent->cf;
	bool succ = true;

	current->fork_sema = parent->fork_sema;
	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
	process_activate (current);

#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt)){
		goto error;
	}
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	
	parent->cf = NULL;
done:;
	/* We arrive here whether the load is successful or not. */
	struct list_elem *e;
	struct open_file *opfile;
	struct open_file *copyopfile;
	if(!list_empty(&parent->opfile_list)){
		for (e = list_begin (& (parent -> opfile_list)); e != list_end (&(parent -> opfile_list)); e = list_next (e)) {
			opfile = list_entry (e, struct open_file, elem);
			copyopfile = malloc(sizeof(struct open_file));
			if(copyopfile == NULL){
				goto error;
			}

			if(opfile->fptr == NULL)
				copyopfile -> fptr = NULL;
			else if(opfile->fptr == -100)
				copyopfile -> fptr = -100;
			else 
				copyopfile -> fptr =  file_duplicate(opfile->fptr);

			copyopfile -> fd = opfile -> fd;
			list_push_back(&(current-> opfile_list), &(copyopfile->elem));
		}
	}
	current->stdin = parent->stdin;
	current->stdout = parent->stdout;
	current->parent = parent;
	struct child_thread* tchild = malloc(sizeof(struct child_thread));
	tchild -> child = current;
	tchild -> tid = current->tid;
	tchild -> wait = -2;
	list_push_back(&(parent->child_list), &(tchild->elem));
	if_.R.rax = 0;
	process_init ();
	
	if (succ){
		sema_up(current->fork_sema);
		parent->fork_sema = NULL;
		current -> fork_sema = NULL;
		intr_set_level(old_level);
		do_iret (&if_);
	}
error:
	parent->fork = TID_ERROR;
	printf("fork error \n");
	thread_exit ();
}
/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char* suck = malloc(strlen(f_name)+1);
	strlcpy(suck, f_name, strlen(f_name)+1);
	bool success;
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	supplemental_page_table_init (&thread_current()->spt);
	
	char *token, *save_ptr;
	char *argv[50];
	const char * file_name = strtok_r (suck, " ", &save_ptr);
	success = load (file_name, &_if);

	if( pg_ofs (f_name) == 0)
		palloc_free_page (f_name);

	if (!success)
		return -1;

	int tokensize;
	int argsize = 0;
	tokensize = strlen(file_name) + 1;
	argv[argsize] = malloc(tokensize);
	strlcpy(argv[argsize], file_name, tokensize);
	argsize++;
	while(token = strtok_r (NULL, " ", &save_ptr)){
		tokensize = strlen(token) + 1;
		argv[argsize] = malloc(tokensize);
		strlcpy(argv[argsize], token, tokensize);
		argsize++;
	}

	/* And then load the binary */
	uint64_t argadd[50];
	for(int i=argsize-1;i>=0;i--){
		tokensize = strlen(argv[i]) + 1;
		_if.rsp -= tokensize;
		memcpy(_if.rsp, argv[i], tokensize);
		argadd[i] = _if.rsp;
		free(argv[i]);
	}
	while(_if.rsp%8 !=0){
		_if.rsp -= 1;
		memset(_if.rsp, 0, 1);
	}

	_if.rsp -= 8;
	memset(_if.rsp, 0, 8);

	for(int i=argsize-1;i>=0;i--){
		_if.rsp -= 8;
		memcpy(_if.rsp, &argadd[i], 8);
	}
	
	_if.R.rsi = _if.rsp;
	_if.rsp -= 8;
	memset(_if.rsp, 0, 8);
	_if.R.rdi = argsize;
	free(suck);
	
	struct open_file* ttfile = malloc(sizeof(struct open_file));
	ttfile -> fd = 0;
	ttfile -> fptr = -100;
	list_push_back(&(thread_current()->opfile_list), &(ttfile->elem));


	struct open_file* tfile = malloc(sizeof(struct open_file));
	tfile -> fd = 1;
	tfile -> fptr = NULL;
	list_push_back(&(thread_current()->opfile_list), &(tfile->elem));
	
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread* curr = thread_current();
	struct semaphore sema;
	sema_init(&sema, 0);
	
	struct list_elem *e;
	struct child_thread *f;
	enum intr_level old_level = intr_disable();
	int chk = 0;
	if(!list_empty (&curr->child_list)) {
		for (e = list_begin (&curr->child_list); e != list_end (&curr->child_list); e = list_next (e)) {
			f = list_entry (e, struct child_thread, elem);
			if(f->tid == child_tid && f->wait == -2){
				f->child->wait_sema  = &sema;
				chk = 1;
				sema_down(&sema);
				break;
			}
		}
	}
	if(curr->tid == 1){
		curr->wait_sema = &sema;
		sema_down(&sema);
		curr->wait_sema = NULL;
	}
	
	intr_set_level(old_level);

	int wvalue = -1;
	struct child_thread *child;
	for (e = list_begin (& (curr->child_list)); e != list_end (&(curr->child_list)); e = list_next (e)) {
		child= list_entry (e, struct child_thread, elem);
		if(child->tid == child_tid){
			wvalue = child->wait;
			list_remove(e);
			free(child);
			break;
		}
	}
	return wvalue;
}


/* Exit the process. This function is called by thread_exit (). */
void
process_exit () {
	
	enum intr_level old_level = intr_disable();
	struct thread *curr = thread_current ();
	struct list_elem *e2;
	struct list_elem *e3;
	struct mm_file* f;
	struct page *page = NULL;
	struct page_addr* paddr;
	for (e2 = list_begin (&(curr->mmfile_list)); e2 != list_end (&(curr->mmfile_list)); e2 = list_next (e2)) {
		f = list_entry (e2, struct mm_file, elem);
		for (e3 = list_begin (&(f -> page_list)); e3 != list_end (&(f-> page_list));) {
			paddr = list_entry (e3, struct page_addr, elem);
			page = spt_find_page(&curr->spt, paddr->addr);
			e3 = list_next (e3);
			if (page){
				if(pml4_is_dirty(curr->pml4, page->va)){
					intr_set_level(old_level);
					file_write_at(paddr -> file, paddr -> addr, paddr -> size, paddr -> offset);
					old_level = intr_disable();
				}
				hash_delete(curr->spt.hash_table, &(page->elem));
				free(paddr);
			}
		}
	}
	if(curr->pml4 == NULL)
		goto die;
	if(curr->parent->tid == 1){
		if(curr->parent->wait_sema != NULL){
			sema_up(curr->parent->wait_sema);
		}
	}
	if(curr->wait_sema != NULL){
		sema_up(curr->wait_sema);
	}
	
	if(curr->fork_sema != NULL){
		sema_up(curr->fork_sema);
		curr->parent->fork_sema = NULL;
	}

	if(curr->parent != NULL){
		struct list_elem *e;
		struct child_thread *child;
		for (e = list_begin (& (curr->parent->child_list)); e != list_end (&(curr->parent->child_list)); e = list_next (e)) {
			child= list_entry (e, struct child_thread, elem);
			if(child->tid == curr->tid){
				child->wait = curr->exit;
				break;
			}
		}
		curr->parent = NULL;
	}
	if(curr->exit == -1){
		printf ("%s: exit(-1)\n", curr->name);
	}
	else{
		printf ("%s: exit(%d)\n", curr->name, curr->exit);
	}
die : ;
	struct list_elem *e;
	struct open_file *opfile;
	struct list_elem *ep;
	struct open_file *epfile;
	struct file* erase;
	uint64_t fplace = 0xcccccccccccccccc;
	int j;
	bool pa;
	file_lock_release();
	file_lock_acquire();
	struct file* closelist[128];
	int ccounter = 0;

	for (e = list_begin (& (curr->opfile_list)); e != list_end (&(curr->opfile_list)); e = list_begin (& (curr->opfile_list))) {
		opfile = list_entry (e, struct open_file, elem);
		erase = opfile->fptr;
		pa = true;
		if(erase != NULL && erase != -100){

			for(j=0; j<ccounter; j++){
				if(erase == closelist[j]){
					pa = false;
					break;
				}
			}
			if(pa){
				file_close(erase);
				closelist[ccounter]=erase;
				ccounter++;
			}
		}
		list_remove(e);
		free(opfile);
	}
	print_oplist();
	
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	
	process_cleanup ();
	file_lock_release();
	intr_set_level(old_level);
	process_thread_exit();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */ 
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file_lock_acquire();
	file = filesys_open (file_name);
	file_lock_release();
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		printf("%d listsize\n", list_size(&thread_current ()->mmfile_list));
		process_exit();
		printf("returned from exit!\n");
		return false;
	}

	/* Read and verify executable header. */
	file_lock_acquire();
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		file_lock_release();
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}
	file_lock_release();
	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;
		file_lock_acquire();
		if (file_ofs < 0 || file_ofs > file_length (file)){
			file_lock_release();
			goto done;
		}
		file_lock_release();
		file_lock_acquire();
		file_seek (file, file_ofs);
		file_lock_release();
		file_lock_acquire();
		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr){
			file_lock_release();
			goto done;
		}
		file_lock_release();
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */


/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);
	
	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		file_stat(file);

		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
struct lazyload{
	struct file *file;
	uint8_t *upage;
	uint32_t read_bytes; 
	uint32_t zero_bytes;
	off_t ofs;
	bool writable;
};

static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();
	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	pml4_clear_page(t->pml4, upage);
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}


static bool
lazy_load_segment (struct page *page, void *aux1) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct lazyload * aux = (struct lazyload *) aux1;

	file_seek (aux->file, aux->ofs); 
	/* Load this page. */
	uint8_t *kpage = page->frame->kva;
	
	if (file_read (aux->file, kpage, aux->read_bytes) != (int) aux->read_bytes) {
		palloc_free_page (kpage);
		printf("false pass1!\n");
		return false;
	}
	/* Add the page to the process's address space. */	
	memset (kpage + aux -> read_bytes, 0, aux -> zero_bytes);
	if (!install_page (aux->upage, kpage, aux->writable)) {
		palloc_free_page (kpage);
		printf("false pass2!\n");
		return false;
	}
	page->frame->kva = pml4_get_page(thread_current()->pml4, page->va);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	if(pml4_is_dirty(thread_current()->pml4, page->va)){
		printf("born dirty page\n");
	}
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	off_t ofs_check = ofs;
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */	
		

		void *aux = NULL;
		struct lazyload *aux1 = (struct lazyload *)malloc(sizeof(struct lazyload));
		aux1 -> file = (uint64_t)file;
		aux1 -> upage = upage;
		aux1 -> read_bytes = page_read_bytes;
		aux1 -> zero_bytes = page_zero_bytes;
		aux1 -> writable = writable;

		aux1 -> ofs = ofs_check;
		ofs_check += (page_read_bytes);
		aux = aux1;
		
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;
		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	struct page *kpage= malloc(sizeof(struct page));
	struct frame *frame = malloc(sizeof(struct frame));
	uint8_t *kva = palloc_get_page (PAL_USER | PAL_ZERO);
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */ 
	frame -> page = kpage;
	kpage -> frame = frame;
	kpage -> writable = true;

	kpage -> va = stack_bottom;
	if (kpage != NULL) {
		success = install_page (stack_bottom, kva, true);
		if (success){
			if_->rsp = USER_STACK;
			frame->kva = pml4_get_page(thread_current()->pml4, kpage->va);
		}
		else
			palloc_free_page (frame);
	}
	kpage->frame->kva = kva;
	anon_initializer(kpage, VM_ANON, NULL);
	spt_insert_page(&thread_current()->spt, kpage);
	kpage -> is_stack = true;
	return success;
}
#endif /* VM */
