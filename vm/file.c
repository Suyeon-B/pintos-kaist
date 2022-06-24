/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/userprog/process.h"
#include "include/threads/mmu.h"
#include "threads/malloc.h"
#include "include/userprog/syscall.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	free(page->frame); /* frame 할당 해제 */
}

void *undo_mmap(void *initial_addr, void *addr)
{
	struct thread *curr_thread = thread_current();
	struct supplemental_page_table *spt = &curr_thread->spt;
	struct page *page;
	while (initial_addr < addr)
	{
		page = spt_find_page(spt, addr);
		spt_remove_page(spt, page);
		initial_addr += PGSIZE;
	}
	return NULL;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t read_bytes, int writable,
		struct file *file, off_t offset)
{
	struct thread *curr_thread = thread_current();
	struct supplemental_page_table *spt = &curr_thread->spt;
	void *initial_addr = addr;

	while (read_bytes > 0)
	{
		/* to avoid overlap */
		if (spt_find_page(&thread_current()->spt, addr))
		{
			/* 만약 overlap이 발생하면, 할당한 페이지 모두 할당 해제 */
			undo_mmap(initial_addr, addr);
		}
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)malloc(sizeof(struct aux_for_lazy_load));
		aux->load_file = file;
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux))
		{
			free(aux);
			undo_mmap(initial_addr, addr);
		}

		read_bytes -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return initial_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	struct thread *curr = thread_current();
	struct page *page;

	if ((page = spt_find_page(&curr->spt, addr)) == NULL)
	{
		return;
	}

	struct file *file = ((struct aux_for_lazy_load *)page->uninit.aux)->load_file;

	// SJ
	if (!file)
	{
		return;
	}

	while (page != NULL && page_get_type(page) == VM_FILE)
	{
		if (pml4_is_dirty(curr->pml4, page->va))
		{
			lock_acquire(&file_lock);
			struct aux_for_lazy_load *aux = page->uninit.aux;
			file_write_at(aux->load_file, addr, aux->read_bytes, aux->offset);
			pml4_set_dirty(curr->pml4, page->va, false);
			lock_release(&file_lock);
		}

		pml4_clear_page(&curr->pml4, addr);
		addr += PGSIZE;
		page = spt_find_page(&curr->spt, addr);
	}
}