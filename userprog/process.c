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
#include "userprog/syscall.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE.
 * = ppt에서 process_execute() */
tid_t process_create_initd(const char *file_name)
{
	//실행파일의 이름을 가져온다.
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	memcpy(fn_copy, file_name, PGSIZE);

	/* 첫번째 공백 전까지(파일명)의 문자열 파싱 */
	char *save_ptr;						 /* 분리되고 남은 문자열 */
	strtok_r(file_name, " ", &save_ptr); /* 첫번째 인자 */

	/* 실행하려는 파일의 이름을 스레드의 이름으로 전달하고,
	   실행(initd)기능을 사용하여 스레드를 생성한다. */
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);

	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);

	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif
	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* Clone current thread to new thread.*/
	/* cur = 부모 프로세스(Caller) */
	struct thread *curr = thread_current();
	memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

	/* 새롭게 프로세스를 하나 더 만든다. 자식 프로세스는 __do_fork()를 수행한다. */
	tid_t tid = thread_create(name, curr->priority, __do_fork, curr);
	if (tid == TID_ERROR)
		return TID_ERROR;

	/* thread_create하면서 부모 프로세스의 자식 list에 넣어주었다. */
	struct thread *child = get_child_by_tid(tid);

	/* 자식이 fork를 끝낼 때까지 기다린다. */
	sema_down(&child->sema_fork); /* wait until child loads */

	if (child->exit_status == -1)
	{
		return TID_ERROR;
	}

	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
	{
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
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
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	/* 부모의 인터럽트 프레임(CPU context)을 복사해온다. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent. */

	/* --- PROJECT 2 : system call ------------------------------ */
	if (parent->next_fd == FD_LIMIT)
	{
		goto error;
	}

	/* 부모의 FDT 복사 */
	for (int i = 2; i < FD_LIMIT; i++)
	{ /* ! 여기 0이 아니라 2부터도 돌려보기 */
		struct file *file = parent->fdt[i];
		if (file == NULL)
		{

			continue;
		}
		bool found = false;
		if (!found)
		{
			struct file *new_file;
			if (file > 2)
			{
				new_file = file_duplicate(file);
			}
			else
			{
				new_file = file;
			}
			current->fdt[i] = new_file;
		}
	}
	current->next_fd = parent->next_fd;

	sema_up(&current->sema_fork);
	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->sema_fork);
	exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail.
 * run_task에서 호출 시작 프로세스
 * 인터럽트 프레임과 사용자 스택을 초기화 한다.
 * 사용자 스택에서 arguments 설정
 * 인터럽트 종료를 통해 유저프로그램으로 점프 */
int process_exec(void *f_name) /* 프로세스 실행 - 실행하려는 바이너리 파일 이름을 가져옴 */
{
	char *file_name = f_name;
	char *file_name_copy;
	bool success;

	memcpy(file_name_copy, file_name, strlen(file_name) + 1);

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	/* 파싱하기 */
	int token_count = 0;
	char *token, *last;
	char *arg_list[65]; /* \0 포함 최대 65개 */

	token = strtok_r(file_name_copy, " ", &last);
	arg_list[token_count] = token;

	while (token != NULL)
	{
		token = strtok_r(NULL, " ", &last);
		token_count++;
		arg_list[token_count] = token;
	}

	/* 해당 바이너리 파일을 메모리에 로드하기 */
	success = load(arg_list[0], &_if);

	/* If load failed, quit. */
	palloc_free_page(file_name);
	if (!success)
		return -1;

	/* 유저 프로그램이 실행되기 전에 스택에 인자 저장 */
	argument_stack(token_count, arg_list, &_if);

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)*rspp, true);  - for debug
	/* Start switched process. */
	do_iret(&_if); /* 유저 프로그램 실행 */
	NOT_REACHED();
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
int process_wait(tid_t child_tid UNUSED)
{
	/* 자식 프로세스의 프로세스 디스크립터(struct thread) 검색 */
	struct thread *child = get_child_by_tid(child_tid);

	/* If TID is invalid */
	if (child == NULL)
	{
		return -1;
	}

	/* 자식프로세스가 종료될 때까지 부모 프로세스 대기(세마포어 이용) */
	sema_down(&child->sema_wait);
	int child_exit_status = child->exit_status;
	/* 자식 프로세스 디스크립터 삭제 */
	list_remove(&child->child_elem);
	/* c_thread가 삭제되어 오면 remove를 할 수 없으니,
	 * sema up은 fdt에서 삭제까지 마친 뒤에 한다. */
	sema_up(&child->sema_exit);

	/* 자식 프로세스의 exit status 리턴 */
	return child_exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	for (int i = 0; i < FD_LIMIT; i++)
	{
		close(i);
	}
	file_close(curr->running_file);				/* running file 닫기 */
	palloc_free_multiple(curr->fdt, FDT_PAGES); /* fd_table 반환 */

	sema_up(&curr->sema_wait);	 /* wait하고 있을 parent를 위해 */
	sema_down(&curr->sema_exit); /* 부모 스레드의 자식 list에서 지워질 때 까지 기다림 */
	process_cleanup();			 /* ! 이거 세마 밑에 있어야되는 거 아님? */
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
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

struct ELF64_PHDR
{
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

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{ /* 사용자 스택 유형, 함수의 시작진입점등을 포함한다. */
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create(); /* 유저 프로세스의 페이지 테이블 생성 */
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current()); /* 레지스터 값을 실행중인 스레드의 페이지 테이블 주소로 변경 */

	/* Open executable file. */
	file = filesys_open(file_name); /* 프로그램 파일 오픈 */

	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header.
	 * ELF파일의 헤더 정보를 읽어와 저장
	 * 이 때 write 중인 파일은 lock */
	t->running_file = file;
	file_deny_write(file);
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers.
	 * 배치 정보를 읽어와 저장 */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
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
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				/* 배치 정보를 통해 파일을 메모리에 탑재 */
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_)) //진입점을 초기화하기 위한 코드(스택 진입점)
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry; //

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
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

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
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
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
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
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* Load this page. */
	struct aux_for_lazy_load *lazy_load = (struct aux_for_lazy_load *)aux;
	struct file *file = lazy_load->load_file;
	size_t offset = lazy_load->offset;
	size_t read_bytes = lazy_load->read_bytes;
	size_t zero_bytes = lazy_load->zero_bytes;

	file_seek(file, offset);
	if (file_read(file, page->frame->kva, read_bytes) != (int)read_bytes)
	{
		return false;
	}
	memset(page->frame->kva + read_bytes, 0, zero_bytes);
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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		/* 여기서 고민한 점
		 * 1. load_segment → vm_alloc_page_with_initializer를 통해 기대하는 결과는?
		 * 2. upage와 kpage의 구분
		 * 3. lazy_load_segment에 넘겨줄 aux가 뭘까
		 * 4. lazy loading을 위해 파일 정보를 임시로 저장해둔 뒤,
		 *    lazy_load_segment로 보내 한꺼번에 로드하고 싶다면 새로운 구조체를 만들어야할까 */
		struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)malloc(sizeof(struct aux_for_lazy_load));

		/* page fault 시에만 lazy load */
		aux->load_file = file;
		aux->offset = ofs;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		/* Set up aux to pass information to the lazy_load_segment. */
		if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, aux))
		{
			free(aux);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	if (!vm_alloc_page(VM_MARKER_0 | VM_ANON, stack_bottom, true))
	{
		return false;
	}

	success = vm_claim_page(stack_bottom);

	if (success)
	{
		if_->rsp = USER_STACK;
		thread_current()->stack_bottom = stack_bottom;
	}

	return success;
}
#endif /* VM */

void argument_stack(int argc, char **argv, struct intr_frame *if_)
{
	/*
	argv : 프로그램 이름과 인자가 저장되어 있는 메모리 공간
	argc : 인자의 개수
	if_ : 스택 포인터를 가리키는 주소 값을 저장할 intr_frame
	*/
	int i;
	char *argu_addr[128];
	int argc_len;

	for (i = argc - 1; i >= 0; i--)
	{
		argc_len = strlen(argv[i]);
		if_->rsp = if_->rsp - (argc_len + 1);
		memcpy(if_->rsp, argv[i], (argc_len + 1));
		argu_addr[i] = if_->rsp;
	}

	while (if_->rsp % 8 != 0)
	{
		if_->rsp--;
		memset(if_->rsp, 0, sizeof(uint8_t));
	}

	for (i = argc; i >= 0; i--)
	{
		if_->rsp = if_->rsp - 8;
		if (i == argc)
		{
			memset(if_->rsp, 0, sizeof(char **));
		}
		else
		{
			memcpy(if_->rsp, &argu_addr[i], sizeof(char **));
		}
	}

	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(void *));

	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp + 8;
}

/* Find available spot in fd_table, put file in  */
int add_file_to_fdt(struct file *file)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;

	/* file 포인터를 fd_table안에 넣을 index 찾기 */
	while (curr->next_fd < FD_LIMIT && fdt[curr->next_fd])
	{
		curr->next_fd++;
	}

	if (curr->next_fd >= FD_LIMIT)
	{
		return -1;
	}

	fdt[curr->next_fd] = file;
	return curr->next_fd;
}

struct file *process_get_file(int fd)
{
	if (fd < 0 || fd >= FD_LIMIT)
		return NULL;
	struct thread *curr = thread_current();
	return curr->fdt[fd];
}

/* Remove give fd from current thread fd_table */
void remove_file_from_fdt(int fd)
{
	if (fd < 0 || fd >= FD_LIMIT) /* Error - invalid fd */
		return;

	struct thread *cur = thread_current();
	cur->fdt[fd] = NULL;
}

void process_close_file(int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{
		return;
	}
	remove_file_from_fdt(fd);
	file_close(file_obj);
}