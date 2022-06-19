/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/userprog/process.h"
#include "include/threads/mmu.h"
#include "threads/malloc.h"

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

/* Do the mmap */
void *
do_mmap(void *addr, size_t read_bytes, int writable,
		struct file *file, off_t offset)
{

	while (read_bytes > 0)
	{
		if (spt_find_page(&thread_current()->spt, addr))
		{
			return NULL;
		}
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)malloc(sizeof(struct aux_for_lazy_load));
		aux->load_file = file;
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		// printf("\n\n### file addr in mmap: %p", aux->load_file);
		// printf("\n\n### read_bytes in mmap: %p", aux->read_bytes);

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux))
		{
			free(aux);
			return false;
		}

		read_bytes -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	struct page *page = spt_find_page(&spt, addr);
	if (!page || page_get_type(page) != VM_FILE)
	{
		return;
	}

	struct file_page *file_page = &page->uninit;
	struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)(file_page->aux);
	struct file *mmap_file = (struct file *)malloc(sizeof(mmap_file)); /* 수상함 */
	memcpy(mmap_file, aux->load_file, sizeof(mmap_file));

	while (mmap_file == aux->load_file)
	{
		/* if the file is dirty */
		if (pml4_is_dirty(curr->pml4, aux->load_file))
		{
			/* buffer의 내용을 file의 offset부터  */
			file_write_at(aux->load_file, addr, aux->read_bytes, aux->offset);
		}
		pml4_clear_page(curr->pml4, addr);
		spt_remove_page(spt, page);

		addr += PGSIZE;
		page = spt_find_page(spt, addr);

		if (!page)
		{
			return;
		}

		file_page = &page->file;
		aux = (struct aux_for_lazy_load *)(file_page->aux);
	}
}
