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

/* 추가로 선언한 부분 */
#include "userprog/process.h"
#include "filesys/file.h"
#include <string.h>
#include "threads/palloc.h"

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
	switch (f->R.rax) /* rax : system call number */
	{
	/* Projects 2 and later. */
	/* rdi, rsi, rdx, r10, r8, and r9 순으로 argument passing */
	/* system call 반환값은 rax에 담아준다. */
	case SYS_HALT: /* Halt the operating system. */
		halt();
		break;
	case SYS_EXIT: /* Terminate this process. */
		exit(f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC: /* Switch current process. */
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT: /* Wait for a child process to die. */
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE: /* Create a file. */
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE: /* Delete a file. */
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN: /* Open a file. */
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE: /* Obtain a file's size. */
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ: /* Read from a file. */
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE: /* Write to a file. */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: /* Change position in a file. */
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL: /* Report current position in a file. */
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE: /* Close a file. */
		close(f->R.rdi);
		break;
	default:
		thread_exit();
	}
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
   유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
void check_address(void *addr)
{
	if (!is_user_vaddr(addr) ||
		!(pml4_get_page(thread_current()->pml4, addr)))
	{
		exit(-1);
	}
}

/* PintOS를 종료시킨다. */
void halt(void)
{
	power_off();
}

/* Save exit status at process descriptor */
/* 현재 스레드 상태를 exit status로 저장하고,
   종료 메세지와 함께 스레드를 종료시킨다. */
void exit(int status)
{
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

/* 주소값이 user 영역에 속하는지 확인하고,
   맞다면 파일을 생성한다.
   return value : T/F */
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	if (*file == NULL)
	{
		exit(-1);
	}
	return filesys_create(file, initial_size);
}

/* 파일을 삭제한다.
   return value : T/F */
bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

/* 새롭게 프로그램을 실행시키는 시스템 콜
   return value : pid / -1 */
int exec(const char *cmd_line)
{
	/* 새롭게 할당받아 프로그램을 실행시킨다. */
	check_address(cmd_line);
	char *fn_copy;
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return -1;
	strlcpy(fn_copy, cmd_line, PGSIZE);

	char *save_ptr;
	strtok_r(cmd_line, " ", &save_ptr);
	if (process_exec(fn_copy) == -1)
	{
		return -1; /* exec 실패 시에만 리턴 */
	}
	NOT_REACHED();
}

/* 자식 프로세스가 종료 될 때까지 대기
   return value : 정상종료 exit status / -1 */
int wait(pid_t pid)
{
	return process_wait(pid);
}

pid_t fork(const char *thread_name)
{

	// struct intr_frame *if_;

	// int c_tid = process_fork(thread_name, if_);
	// if (c_tid){
	// 	return process_exec(thread_name);
	// }
	// return TID_ERROR;
}

/* 사용자 프로세스가 파일에 접근하기 위한 시스템콜
   return value : fd/-1 */
int open(const char *file)
{
	check_address(file);
	struct file *open_file = filesys_open(file);
	if (!open_file)
	{
		return -1;
	}
	else
	{
		int fd = process_add_file(open_file); // 오픈한 파일을 스레드 내 fdt테이블에 추가 - 스레드가 파일을 관리할수있게
		if (fd == -1)
		{
			file_close(open_file);
		}
		return fd;
	}
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

	unsigned char *buf = buffer; // 1바이트씩 저장하기 위해

	if (fd == 0) // STDIN_FILENO : 사용자 입력 읽기
	{
		for (count = 0; count < length; count++)
		{
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
	check_address(buffer);
	struct file *curr_file = process_get_file(fd);
	int read_count;
	if (fd == 1) // STDOUT_FILENO
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
	if (curr_file)
	{
		file_seek(curr_file, position);
	}
}

unsigned tell(int fd)
{
	struct file *curr_file = process_get_file(fd);
	if (curr_file)
	{
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