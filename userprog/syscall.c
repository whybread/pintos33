#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */


// open thread files
typedef int pid_t;

//함수 추가
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int dup2(int oldfd, int newfd);

// project 3
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);
// void print_regi(struct intr_frame *f);

int64_t find_file(int fd);
int64_t remove_file(int fd);
struct open_file* get_opfile(int fd);
// struct file_sema* find_sema(const char *file);
void print_file();
void print_oplist();

struct lock sys_lock;

void
syscall_init (void) {
	lock_init (&sys_lock);


	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
		// printf ("system call! : %s, rsi %llx\n", thread_current()->name, f->R.rsi);

	if(  !is_user_vaddr(f->R.rax) )
		process_exit();
	uint64_t syscallnum = f->R.rax;
	// if(syscallnum != 7 && syscallnum != 21)
	// printf("@syscallnum: %lld at %s\n", syscallnum, thread_current()->name);
	// printf("-------------------------------------\n");
	// print_regi(f);
	// printf("----\n");
	// uint64_t tt = 0x80042082aa;
	// printf("%lld\n", &tt);
	// print_regi(&thread_current()->tf);
	// printf("-------------------------------------\n");
	// hex_dump(f -> rsp, f -> rsp, USER_STACK - f->rsp, true);
	// %rdi, %rsi, %rdx, %r10, %r8, %r9.
	switch(syscallnum) {
		case SYS_HALT : //0
			halt();
			break;
		case SYS_EXIT : {//1
			int status = (int) f->R.rdi;
			exit (status);
			break;
		}
		case SYS_FORK : {//2
			// print_regi(f);
			enum intr_level old_level = intr_disable();
			const char * thread_name = f->R.rdi;
			f-> R.rax = fork (thread_name, f);
			intr_set_level(old_level);
			break;
		}
		case SYS_EXEC : {
			// lock_acquire(&exc_lock);
			const char *file = f->R.rdi; 
			// printf("exec %s \n", file);
			f-> R.rax = exec (file);
			// lock_release (&exc_lock);
			break;
		}
		case SYS_WAIT : {
			pid_t pid = f->R.rdi; 
			f-> R.rax = wait (pid);
			break;
		}
		case SYS_CREATE :{//5
			const char *file = f->R.rdi; 
			unsigned initial_size = f->R.rsi; 
			f-> R.rax = create (file, initial_size);
			break;
		}
		case SYS_REMOVE :{
			const char *file = f->R.rdi; 
			f-> R.rax = remove (file);
			break;}
		case SYS_OPEN :{//7
			const char *file = f->R.rdi; 
			int temp = open(file);
			// printf("%d\n", temp);
			f-> R.rax = temp;
			break;}
		case SYS_FILESIZE :{
			int fd = f->R.rdi;
			f-> R.rax = filesize (fd);
			file_lock_release();
			break;}
		case SYS_READ :{
			// printf("----readcalled----\n");
			int fd = f->R.rdi;
			void *buffer = f->R.rsi;
			unsigned size = f->R.rdx;
			// printf("%d,%llx,%d,%s\n", fd, buffer, size, thread_current()->name);
			int temp = read (fd, buffer, size);
			// printf("read return %d\n", temp);
			f-> R.rax = temp;
			break;}  
		case SYS_WRITE :{//10
			int fd = f->R.rdi;
			const void *buffer = f->R.rsi;
			unsigned size = f->R.rdx;
			// printf("%d , %s , %d\n",fd, buffer , size);
			f-> R.rax = write(fd, buffer, size);
			break;}
		case SYS_SEEK :{
			int fd = f->R.rdi;
			unsigned position = f->R.rsi;
			seek (fd, position);
			break;}
		case SYS_TELL :{
			int fd = f->R.rdi;
			f-> R.rax = tell (fd);
			break;}
		case SYS_CLOSE :{
			int fd = f->R.rdi;
			close (fd);
			break;}
		case SYS_DUP2 :{
			int oldfd = f->R.rdi;
			int newfd = f->R.rsi;
			f-> R.rax = dup2 (oldfd, newfd);
			break;}
		case SYS_MMAP :{
			void* addr = f->R.rdi;
			size_t length = f->R.rsi;
			int writable = f->R.rdx;
			int fd = f->R.r10;
			off_t offset = f->R.r8;
			f-> R.rax = mmap (addr, length, writable, fd, offset);
			break;}
		case SYS_MUNMAP :{
			void* addr = f->R.rdi;
			munmap (addr);
			break;}	
	}
	// thread_exit ();
}

//-------------------------------------------

void
halt (void) {
	power_off();
	NOT_REACHED ();
}

void
exit (int status) {
	// printf("\n");
	// printf("### wait exe! %d \n", status);
	// printf("\n");
	thread_current()->exit = status;
	thread_exit();
}

pid_t
fork (const char *thread_name, struct intr_frame *f){
	// printf("fork name is : %s \n", thread_name);
	pid_t temp = process_fork(thread_name, f);
	return temp;
}

int
exec (const char *file) {
	if ( !pml4_get_page(thread_current()->pml4, file) )
		thread_exit();

	char name[16];
	int i;

	for(i=0; i<16; i++){
		if(file[i]==' ' || file[i]=='\0'){
			name[i] = '\0';
			break;
		}
		name[i] = file[i];
	}
	file_lock_acquire();
	struct file *files= filesys_open (name);
	file_lock_release();
	if (files == NULL) {
		printf ("load: %s: open failed\n", name);
		process_exit();
	}
	file_close(files);

	return process_exec(file);
}

int
wait (pid_t pid) {
	return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size) {
	if ( !pml4_get_page(thread_current()->pml4, file) ){
		thread_exit();
	}
	file_lock_acquire();
	bool ret_val = filesys_create(file, initial_size);
	file_lock_release();
	return ret_val;
}

bool
remove (const char *file) {
	file_lock_acquire();
	bool ret_val = filesys_remove(file);
	file_lock_release();
	return ret_val;
}

// struct file_sema*
// find_sema(const char *file){
// 	struct list_elem *e;
// 	struct file_sema *opfile;
// 	if(list_empty(&file_sema_list))
// 		return NULL;
// 	for (e = list_begin (&file_sema_list); e != list_end (&file_sema_list); e = list_next (e)) {
// 		opfile = list_entry (e, struct file_sema, elem);
// 		if(strcmp(file, opfile->name)){
// 			return opfile;
// 		}
// 	}
// 	return NULL;
// }
// void
// print_file(){
// 	struct list_elem *e;
// 	struct file_sema *opfile;
// 	if(list_empty(&file_sema_list))
// 		return 0;
// 	for (e = list_begin (&file_sema_list); e != list_end (&file_sema_list); e = list_next (e)) {
// 		opfile = list_entry (e, struct file_sema, elem);
// 		printf("%s        %llx\n", opfile->name, opfile->sema);
// 	}
// }
void
print_oplist(){
	struct list_elem *e;
	struct open_file *opfile;
	if(list_empty(&(thread_current()->opfile_list)))
		return;
	for (e = list_begin (&thread_current()->opfile_list); e != list_end (&thread_current()->opfile_list); e = list_next (e)) {
		opfile = list_entry (e, struct open_file, elem);
		// printf("%-10d %llx\n", opfile->fd, opfile->fptr);
	}
}

int
open (const char *file) {   //얘 7번

	// printf("----open called----\n");
	// printf("open: %s\n", file);
	if( file == NULL){
		// printf("null file\n");
		thread_exit();
	}
	if(  !is_user_vaddr(file) ){
		// printf("kernel address\n");
		thread_exit();
	}
	if ( !pml4_get_page(thread_current()->pml4, file) ){
		// printf("not in pml4\n");
		thread_exit();
	}

	// enum intr_level old_level = intr_disable();
	file_lock_acquire();

	// intr_set_level(old_level);
	// printf("####a###\n");
	// printf("%s\n", file);
	struct file* temp_file = filesys_open(file);
	// printf("####b###\n");
	// old_level = intr_disable();
	// file_lock_release();
	// int ss = file_length(temp_file);
	// printf("file %llx %d \n", temp_file, ss);
	// printf("intoif\n");
	if (temp_file == NULL){
		// intr_set_level(old_level);
		file_lock_release();
		// printf("null file\n");
		return -1;
	}
	else{
		if(strcmp(file, thread_current()->name) ==0){
			file_deny_write(temp_file);
		}
		struct open_file* tfile = malloc(sizeof(struct open_file));
		tfile -> fd = create_file_descriptor();
		tfile -> fptr = temp_file;
		list_push_back(&(thread_current()->opfile_list), &(tfile->elem));
		// printf("%d size\n",list_size(&thread_current()->opfile_list));
		// printf("open %llx %d\n", tfile->fptr, tfile->sema->value);
		// print_list();
		int temp = tfile -> fd;
		// printf("returning %d as fd\n", temp);
		// sema_up(fs -> sema);
		file_lock_release();
		// intr_set_level(old_level);
		return temp;
	}
	file_lock_release();
	return -1;
}

int
filesize (int fd) {
	struct file* temp_file = (struct file* ) find_file(fd);
	file_lock_acquire();
	if((temp_file == NULL || temp_file == -1)|| temp_file == -100)
		return -1;
	return file_length(temp_file);
}

int
read (int fd, void *buffer, unsigned size) {
	if(!is_user_vaddr(buffer)||!spt_find_page(&thread_current()->spt, buffer)->writable){
		thread_exit();
	}
	file_lock_acquire();
	enum intr_level old_level = intr_disable();

	struct open_file* temp_file = get_opfile(fd);
	uint8_t* buf = buffer;
	if(temp_file == -100){ //input from stdin(keyboard)
		if(thread_current()->stdin == false){
			intr_set_level(old_level);
			file_lock_release();
			return -1;
		}
		int i;
		for(i=0;i<size;i++){
			*(buf+i) = input_getc();
		}
		intr_set_level(old_level);
		file_lock_release();
		return i;
	}
	else if(temp_file == -1){
		intr_set_level(old_level);
		file_lock_release();
		return -2;
	}
	else{ // input from file
		// printf("read %d \n", temp_file);
		// file_lock_acquire();
		// printf("here! valu %d \n", temp_file->sema->value);
		// printf("here! %llx \n", temp_file->sema);
		// sema_down(temp_file->sema);
		intr_set_level(old_level);
		if(temp_file->fptr == NULL){
			file_lock_release();
			// printf("2\n");
			return -1;
		}
		// printf("%llx %llx %llx \n", buffer, buf, size);
		int readsize = file_read(temp_file->fptr, buffer, size);
		// printf("read %d size\n", readsize);
		// printf("buffer: %s\n", buffer);
		old_level = intr_disable();
		// printf("here! %d \n", list_empty (&temp_file->sema->waiters));
		// file_lock_release();
		// sema_up(temp_file->sema);
		// printf("end!\n");
		file_lock_release();
		intr_set_level(old_level);
		return readsize;
	}
	file_lock_release();
	return -1;
}

int
write (int fd, const void *buffer, unsigned size) {   //이거 10번
	// printf("buff : %llx\n", buffer);
	if ( !pml4_get_page(thread_current()->pml4, buffer) ){
		thread_exit();
	}
	file_lock_acquire();
	enum intr_level old_level = intr_disable();

	
	struct file* temp_file = find_file(fd);


	// printf("temp_file %llx \n", temp_file);
	if(temp_file == NULL){
		if(thread_current()->stdout == false){
			intr_set_level(old_level);
			file_lock_release();
			return -1;
		}
		putbuf (buffer, size);
		intr_set_level(old_level);
		file_lock_release();
		return size;
	}
	else if(temp_file == -1){
		intr_set_level(old_level);
		file_lock_release();
		return -1;
	}
	else if(temp_file == -100){
		intr_set_level(old_level);
		file_lock_release();
		return -1;
	}
	else{
		intr_set_level(old_level);
		int writesize = file_write(temp_file, buffer, size);
		file_lock_release();
		return writesize;
	}
	return 0;
}

void
seek (int fd, unsigned position) {
	// printf("seek %d %d\n", fd, position);
	// print_oplist();
	struct file* temp_file = find_file(fd);
	// printf("temp_file %llx\n", temp_file);
	file_lock_acquire();
	if(temp_file != NULL && temp_file != -1 && temp_file != -100)
		file_seek(temp_file, position);
	file_lock_release();
}

unsigned
tell (int fd) {
	struct file* temp_file = find_file(fd);
	if((temp_file == -1 || temp_file == NULL) || temp_file == -100)
		return -1;
	file_lock_acquire();
	unsigned pos = file_tell(temp_file);
	file_lock_release();
	return pos;
}

void
close (int fd) {
	file_lock_acquire();
	int64_t temp = remove_file(fd);
	file_lock_release();
	if(temp==NULL){
		process_exit();
	}
	// printf("close %d\n", fd);
	// print_oplist();
}

int
dup2(int oldfd, int newfd){
	// printf("fds: %d %d\n", oldfd, newfd);
	// print_oplist();
	// ASSERT(false);
	struct open_file* temp_file = get_opfile(oldfd);
	// thread_current()->dup2 = true;
	if(newfd<0)
		return -1;
	if (temp_file == -1){
		// printf("temp_file is NULL\n");	
		return -1;
	}
	else if(oldfd == newfd){
		return newfd;	
	}
	else{
		struct open_file* check_file = get_opfile(newfd);
		if (check_file != -1){
			remove_file(newfd);
		}
		struct open_file* tfile = malloc(sizeof(struct open_file));
		tfile -> fd = newfd;
		tfile -> fptr = temp_file -> fptr;
		list_push_back(&(thread_current()->opfile_list), &(tfile->elem));
		// printf("d\n");
		return newfd;
	}
}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	// printf("syscall mmap\n");
	if(addr == 0){
		// printf("zero address\n");
		return NULL;
	}
	if(length == 0){
		// printf("zero length\n");
		return NULL;
	}
	if(fd==0 || fd == 1){
		// printf("mmap stdin/stdout\n");
		return NULL;
	}
	if(((int)addr % 0x1000) != 0){
		// printf("zero address\n");
		return NULL;
	}
	struct file* temp_file = find_file(fd);
	struct file* nfile = file_reopen(temp_file);
	return do_mmap(addr, length, writable, nfile, offset);
}

void munmap (void *addr){
	// printf("syscall munmap\n");
	do_munmap(addr);
}



void
print_regi(struct intr_frame *f){
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}

int64_t
find_file(int fd){
	struct thread *curr = thread_current();
	struct list_elem *e;
	struct open_file *opfile;
	for (e = list_begin (& (curr -> opfile_list)); e != list_end (&(curr -> opfile_list)); e = list_next (e)) {
		opfile = list_entry (e, struct open_file, elem);
		if(opfile->fd == fd){
			// printf("find file %d %llx\n", opfile->fd, opfile->fptr);
			return (int64_t) opfile->fptr;
		}
	}
	return -1;
}

struct open_file*
get_opfile(int fd){
	struct thread *curr = thread_current();
	struct list_elem *e;
	struct open_file *opfile;
	for (e = list_begin (& (curr -> opfile_list)); e != list_end (&(curr -> opfile_list)); e = list_next (e)) {
		opfile = list_entry (e, struct open_file, elem);
		if(opfile->fd == fd){
			// printf("opfile te %d  %llx\n", list_empty (&opfile->sema->waiters), opfile->sema);
			return opfile;
		}
	}
	return -1;
}

int64_t
remove_file(int fd){
	struct thread *curr = thread_current();
	struct list_elem *e;
	struct open_file *opfile;
	struct file* cfile = -1;

	for (e = list_begin (& (curr -> opfile_list)); e != list_end (&(curr -> opfile_list)); e = list_next (e)) {
		opfile = list_entry (e, struct open_file, elem);
		if(opfile->fd == fd){
			cfile = opfile->fptr;
			list_remove(e);
			free(opfile);
			break;
		}
	}
	if(cfile == -1)
		return NULL;

	if(!list_empty(& (curr -> opfile_list))){
		for (e = list_begin (& (curr -> opfile_list)); e != list_end (&(curr -> opfile_list));e = list_next (e)) {
			opfile = list_entry (e, struct open_file, elem);
			if(opfile->fptr == cfile){
				return -1;
			}
		}
	}

	if(cfile == NULL){
		thread_current()->stdout = false;
	}
	else if(cfile == -100){
		thread_current()->stdin = false;
	}
	else{
		file_close(cfile);
	}
	return -1;
}



	// // printf("#exe open\n");
	// struct file_sema* fs = find_sema(file);

	// if(fs == NULL){
	// 	// printf("insert %d \n", sizeof(struct file_sema));
	// 	struct semaphore* sema = malloc(sizeof(struct semaphore));
	// 	fs = malloc(sizeof(struct file_sema));
	// 	sema_init(sema, 0);
	// 	fs -> sema = sema;

	// 	for(int i=0; i<16; i++){
	// 		if(file[i]=='\0'){
	// 			fs->name[i]='\0';
	// 			break;
	// 		}
	// 		fs->name[i]=file[i];
	// 	}
	// 	list_push_back(&file_sema_list, &(fs->elem));
	// }
	// else{
	// 	sema_down(fs -> sema);
	// }
	// print_file();
	// printf("called open %s\n", file);
	// file_lock_acquire();
	// printf("file %s\n", file);
