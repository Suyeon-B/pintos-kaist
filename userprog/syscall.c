#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <filesys/filesys.h>

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	printf("system call!\n");
	switch (f->R.rax)
	{
		/* Projects 2 and later. */
		case SYS_HALT: /* Halt the operating system. */
			halt();
			break;
		case SYS_EXIT: /* Terminate this process. */
			exit(f->R.rdi);
			break;
		case SYS_FORK: /* Clone current process. */
			fork(f->R.rdi);
			break;
		case SYS_EXEC: /* Switch current process. */
			exec(f->R.rdi);
			break;
		case SYS_WAIT: /* Wait for a child process to die. */
			wait(f->R.rdi);
			break;
		case SYS_CREATE: /* Create a file. */
			create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE: /* Delete a file. */
			remove(f->R.rdi);
			break;
		case SYS_OPEN: /* Open a file. */
			break;
		case SYS_FILESIZE: /* Obtain a file's size. */
			break;
		case SYS_READ: /* Read from a file. */
			break;
		case SYS_WRITE: /* Write to a file. */
			break;
		case SYS_SEEK: /* Change position in a file. */
			break;
		case SYS_TELL: /* Report current position in a file. */
			break;
		case SYS_CLOSE: /* Close a file. */
			break;
	}
	thread_exit();
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
void check_address(void *addr)
{
	if (!is_user_vaddr(addr))
	{
		exit(-1);
	}
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{ /* 수상함 */
	struct thread *cur = thread_current();
	/* Save exit status at process descriptor */
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

bool create(const char *file, unsigned initial_size)
{ /* 수상함 */
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

pid_t fork(const char *thread_name)
{
}
int exec(const char *file)
{
}
int wait(pid_t pid)
{
}
int open(const char *file)
{
}
int filesize(int fd)
{
}
int read(int fd, void *buffer, unsigned length)
{
}
int write(int fd, const void *buffer, unsigned length)
{
}
void seek(int fd, unsigned position) 
{

}
unsigned tell(int fd) 
{

}
void close(int fd) 
{

}
int dup2(int oldfd, int newfd) 
{

}