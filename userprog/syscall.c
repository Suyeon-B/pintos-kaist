#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* --- PROJECT 2 : system call ------------------------------ */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
#include "include/vm/vm.h"

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

	/* --- PROJECT 2 : system call ------------------------------ */
	lock_init(&file_lock);
	/* ---------------------------------------------------------- */
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/* for stack growth */
	thread_current()->rsp = f->rsp;

	switch (f->R.rax) /* rax : system call number */
	{
	/* Projects 2 and later.
	 * rdi, rsi, rdx, r10, r8, and r9 순으로 argument passing
	 * system call 반환값은 rax에 담아준다. */
	case SYS_HALT: /* Halt the operating system. */
		halt();
		break;
	case SYS_EXIT: /* Terminate this process. */
		exit(f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		f->R.rax = fork(f->R.rdi, f);
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
		exit(-1);
		break;
	}
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
   유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
struct page *
check_address(void *addr)
{
#ifdef VM
	struct page *page = spt_find_page(&thread_current()->spt, addr);

	if (!addr || !(is_user_vaddr(addr)) || !page)
	{
		if (!page && (addr >= thread_current()->rsp - 8 && addr < thread_current()->rsp) || (addr < thread_current()->rsp && addr >= USER_STACK - (1 << 20)))
		{
			if (vm_stack_growth(addr))
			{
				return spt_find_page(&thread_current()->spt, addr);
			}
		}
		exit(-1);
	}
	return page;
#else
	if (!addr || !(is_user_vaddr(addr)) || !pml4_get_page(thread_current()->pml4, addr))
	{
		exit(-1);
	}
#endif
}

/* PintOS를 종료시킨다. */
void halt(void)
{
	power_off();
}

/* 자식 프로세스가 종료 될 때까지 대기
   return value : 정상종료 exit status / -1 */
int wait(tid_t pid)
{
	return process_wait(pid);
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
	char *fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return -1;
	memcpy(fn_copy, cmd_line, strlen(cmd_line) + 1);

	char *save_ptr;
	strtok_r(cmd_line, " ", &save_ptr);
	if (process_exec(fn_copy) == -1)
	{
		return -1; /* exec 실패 시에만 리턴 */
	}
	NOT_REACHED();
}

/* Create new process which is the clone of
   current process with the name THREAD_NAME.

   return value : 부모 - child pid or TID_ERROR
				  자식 - 0 */
tid_t fork(const char *thread_name, struct intr_frame *f)
{
	/* 부모 스레드는 자식 스레드가 복제 완료될 때 까지 리턴하면 x */
	check_address(thread_name);
	return process_fork(thread_name, f);
}

/* 사용자 프로세스가 파일에 접근하기 위한 시스템콜
   return value : fd/-1 */
int open(const char *file)
{
	check_address(file);
	lock_acquire(&file_lock);

	if (file == NULL)
	{
		lock_release(&file_lock);
		return -1;
	}
	struct file *open_file = filesys_open(file);

	if (open_file == NULL)
	{
		lock_release(&file_lock);
		return -1;
	}
	int fd = add_file_to_fdt(open_file); /* 오픈한 파일을 스레드 내 fdt테이블에 추가 - 스레드가 파일을 관리할수있게 */
	if (fd == -1)						 /* FDT가 다 찬 경우 */
	{
		file_close(open_file);
	}
	lock_release(&file_lock);
	return fd;
}

int filesize(int fd)
{
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL)
	{
		return -1;
	}
	return file_length(open_file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_valid_buffer(buffer, size, true);
	lock_acquire(&file_lock);

	int read_result;
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{ /* if no file in fdt, return -1 */
		lock_release(&file_lock);
		return -1;
	}

	/* STDIN */
	if (fd == 0)
	{
		int i;
		char *buf = buffer;
		for (i = 0; i < size; i++)
		{
			char c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}
		read_result = i;
	}
	/* STDOUT */
	else if (fd == 1)
	{
		read_result = -1;
	}
	else
	{
		read_result = file_read(file_obj, buffer, size);
	}
	lock_release(&file_lock);

	return read_result;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_valid_buffer(buffer, size, false);
	lock_acquire(&file_lock);

	int write_result;
	struct file *file_obj = process_get_file(fd);

	if (file_obj == NULL)
	{
		lock_release(&file_lock);
		return -1;
	}

	/* STDOUT */
	if (fd == 1) /* to print buffer strings on the console */
	{
		putbuf(buffer, size);
		write_result = size;
	}
	/* STDIN */
	else if (fd == 0) // write할 수가 없음
	{
		write_result = -1;
	}
	/* FILE */
	else
	{
		write_result = file_write(file_obj, buffer, size);
	}
	lock_release(&file_lock);

	return write_result;
}

void seek(int fd, unsigned position)
{
	if (fd <= 1)
	{
		return;
	}
	struct file *curr_file = process_get_file(fd);
	if (curr_file == NULL)
	{
		return;
	}
	file_seek(curr_file, position);
}

unsigned tell(int fd)
{
	if (fd <= 1)
	{
		return;
	}
	struct file *curr_file = process_get_file(fd);
	if (curr_file == NULL)
	{
		return;
	}
	file_tell(curr_file);
}

void close(int fd)
{
	if (fd <= 1)
	{
		return;
	}
	process_close_file(fd);
}

void check_valid_buffer(void *buffer, unsigned size, bool is_read)
{
	/* 버퍼 내의 시작부터 끝까지의 각 주소를 모두 check_address*/
	for (char i = 0; i < size; i++) /* char OR int */
	{
		struct page *page = check_address(buffer + i);

		/* 해당 주소가 포함된 페이지가 spt에 없거나,
		 * not writable page인 경우 */
		if (is_read && !page->writable)
		{
			exit(-1);
		}
	}
}
