/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

// PJ3
#include "threads/malloc.h"
#include "include/lib/string.h"
#include "threads/mmu.h"
#define PGSIZE (1 << 12)

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);


// PJ3
static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	
	// PJ3
	
	// printf("\n\n ### lazy_load_segment - 1 ### \n\n"); /* 지워 */
	struct aux_for_lazy_load *lazy_load = (struct aux_for_lazy_load *) aux;
	struct file *file = lazy_load->mapped_file;
	size_t ofs = lazy_load->ofs;
	size_t page_read_bytes = lazy_load->page_read_bytes;
	size_t page_zero_bytes = lazy_load->page_zero_bytes;
	
	file_seek(file, ofs);
	// printf("\n\n ### lazy_load_segment - 2 ### \n\n"); /* 지워 */
	// printf("\n\n ### lazy_load_segment - ofs : %d ### \n\n", ofs); /* 지워 */
	// printf("\n\n ### lazy_load_segment - file : %p ### \n\n", file); /* 지워 */
	// printf("\n\n ### lazy_load_segment - page->frame->kva : %p ### \n\n", page->frame->kva); /* 지워 */
	// printf("\n\n ### lazy_load_segment - page_read_bytes : %d ### \n\n", page_read_bytes); /* 지워 */
	if (file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes) {
		// palloc_free_page(page->frame->kva);
		// free(lazy_load);
		// printf("\n\n ### lazy_load_segment - fail ### \n\n"); /* 지워 */
		return false;
	}
	// printf("\n\n ### lazy_load_segment - 3 ### \n\n"); /* 지워 */
	
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	// free(lazy_load);
	// printf("\n\n ### lazy_load_segment - 4 ### \n\n"); /* 지워 */
	return true;
}

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	// PJ3
	// palloc_free_page(page->frame->kva);
	free(page->frame);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t read_bytes, int writable, struct file *file, off_t offset) {
	// PJ3
	while (read_bytes > 0) {
		
		if (spt_find_page(&thread_current()->spt, addr)) {
			return NULL;
		}
		
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)malloc(sizeof (struct aux_for_lazy_load));
		aux->mapped_file = file;
		aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) {
			free(aux);
			return NULL;
		}
		// printf("\n\n ### do_mmap ### \n\n"); /* 지워 */
		// printf("\n\n ### addr : %p ### \n\n", addr); /* 지워 */
		read_bytes -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// PJ3
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	struct supplemental_page_table *spt = &thread_current()->spt;
	if (page == NULL) {
		return;
	}
	
	if (page_get_type(page) != VM_FILE) {
		return;
	}
	
	struct file_page *file_page = &page->file;
	struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)(file_page->aux);
	
	struct file *mmap_file = (struct file *)malloc(sizeof (mmap_file));
	memcpy(mmap_file, aux->mapped_file, sizeof(mmap_file));
	
	while (mmap_file == aux->mapped_file) {
		if (pml4_is_dirty(&thread_current()->pml4, addr) == true) {
			file_write_at(aux->mapped_file, page->frame->kva, aux->page_read_bytes, aux->ofs);
		}
		
		pml4_clear_page(&thread_current()->pml4, addr);
		spt_remove_page(spt, page);
		
		addr += PGSIZE;
		page = spt_find_page(spt, addr);
		
		if (page == NULL) {
			return;
		}
		
		file_page = &page->file;
		aux = (struct aux_for_lazy_load *)(file_page->aux);
	}
	
	free(mmap_file);
}
