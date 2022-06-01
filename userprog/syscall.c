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
// 헤더 선언해야함!!!!!!!!!!!!!!!!!!
/* 수연 추가 */
#include "userprog/process.h"
#include "filesys/file.h"

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

	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	// printf("system call!\n");
	switch (f->R.rax) /* rax : system call number */
	{
	/* Projects 2 and later. */
	/* rdi, rsi, rdx, r10, r8, and r9 순으로 argument passing */
	case SYS_HALT: /* Halt the operating system. */
		// printf("system call - halt!\n");
		halt();
		break;
	case SYS_EXIT: /* Terminate this process. */
		// printf("system call - exit!\n");
		exit(f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		// printf("system call - fork!\n");
		fork(f->R.rdi);
		break;
	case SYS_EXEC: /* Switch current process. */
		// printf("system call - exec!\n");
		exec(f->R.rdi);
		break;
	case SYS_WAIT: /* Wait for a child process to die. */
		// printf("system call - wait!\n");
		wait(f->R.rdi);
		break;
	case SYS_CREATE: /* Create a file. */
		// printf("system call - create!\n");
		create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE: /* Delete a file. */
		// printf("system call - remove!\n");
		remove(f->R.rdi);
		break;
	case SYS_OPEN: /* Open a file. */
		// printf("system call - open!\n");
		open(f->R.rdi);
		break;
	case SYS_FILESIZE: /* Obtain a file's size. */
		// printf("system call - filesize!\n");
		filesize(f->R.rdi);
		break;
	case SYS_READ: /* Read from a file. */
		// printf("system call - read!\n");
		read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE: /* Write to a file. */
		// printf("system call - write!\n");
		write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: /* Change position in a file. */
		// printf("system call - seek!\n");
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL: /* Report current position in a file. */
		// printf("system call - tell!\n");
		tell(f->R.rdi);
		break;
	case SYS_CLOSE: /* Close a file. */
		// printf("system call - close!\n");
		close(f->R.rdi);
		break;
	default:
		// printf("thread_exit - bye~!\n");
		thread_exit();
	}
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
void check_address(void *addr)
{
	if (!is_user_vaddr(addr) || !(pml4_get_page(thread_current()->pml4, addr)))
	{
		exit(-1);
	}
}

/* PintOS를 종료시킨다. */
void halt(void)
{
	power_off();
}

void exit(int status)
{
	/* Save exit status at process descriptor */
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n",thread_name(), status);
	thread_exit();
}

bool create(const char *file, unsigned initial_size)
{ /* 수상함 */
	check_address(file);
	if(*file==NULL){
		exit(-1);
	}
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	if (filesys_remove(file)) {
		return true;
	} else {
		return false;
	}
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

/*
	사용자 프로세스가 파일에 접근하기 위한 시스템콜
	fd 반환
*/
int open(const char *file)
{
	check_address(file);
	if (*file == NULL)
	{
		return -1;
	}
	struct file *open_file = filesys_open(file);
	
	int fd = process_add_file(open_file); // 오픈한 파일을 스레드 내 fdt테이블에 추가 - 스레드가 파일을 관리할수있게
	if(fd == -1){
		file_close(open_file);
	}
	return fd;
}

int filesize(int fd)
{
	struct file *curr_file = process_get_file(fd);
	if (!curr_file)
	{
		return -1;
	}
	return file_length(curr_file);
}
int read(int fd, void *buffer, unsigned length)
{
	check_address(buffer);
	struct file *curr_file = process_get_file(fd);
	char val;
	int count = 0;

	unsigned char *buf = buffer; //1바이트씩 저장하기 위해


	if (fd == 0) //STDIN_FILENO : 사용자 입력 읽기
	{
		for (count=0;count<length;count++){
			val = input_getc(); //키보드 입력받은 문자를 반환하는 함수
			*buf++ = val;
			if (val == '\n')
				break;
		}
	}
	else if (fd == 1) //잘못된 입력
	{
		return -1;
	}
	else // 파일 읽기
	{
		lock_acquire(&filesys_lock);
		count = file_read(curr_file, buffer, length);
		lock_release(&filesys_lock);
	}
	return count;
}

int write(int fd, const void *buffer, unsigned length)
{
	/* 수연 수정 */
	check_address(buffer);
	struct file *curr_file = process_get_file(fd);
	int read_count;
	if (fd == 1) //STDOUT_FILENO 
	{
		putbuf(buffer, length); //문자열을 화면에 출력해주는 함수
		read_count = length;
	}
	else if (fd == 0)
	{
		return -1;
	}
	else
	{
		lock_acquire(&filesys_lock);
		read_count = file_write(curr_file, buffer, length);
		lock_release(&filesys_lock);
	}
	return read_count;
}

void seek(int fd, unsigned position)
{
	struct file *curr_file = process_get_file(fd);
	if (curr_file){
		file_seek(curr_file, position);
	}
}

unsigned tell(int fd)
{
	struct file *curr_file = process_get_file(fd);
	if (curr_file){
		return file_tell(curr_file);
	}
	return -1;
}

void close(int fd)
{
	struct file *curr_file = process_get_file(fd);
	struct thread *curr_thread = thread_current();
	curr_thread->fdt[fd] = 0;
	file_close(curr_file);
}

int dup2(int oldfd, int newfd)
{
}