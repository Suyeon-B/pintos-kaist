/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

// PJ3
#include "include/userprog/process.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "include/userprog/syscall.h"
// #define PGSIZE (1 << 12)

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

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
	
	// PJ3
	// lazy_load_segment랑 비슷하다.
	// alloc 안해도 된다. 이미 존재하는 page가 디스크에 있다가 물리 메모리로 들어가는 거다.
	// 따라서 그냥 물리 메모리에 디스크의 그 저장되어 있단 file-backed page의 내용을 써주면 된다.
	if (page == NULL) {
		return false;
	}
	
	struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)page->uninit.aux;
	
	struct file *file = aux->mapped_file;
	off_t offset = aux->ofs;
	size_t page_read_bytes = aux->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	
	file_seek(file, offset);
	
	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes) {
		return false;
	}
	
	memset(kva + page_read_bytes, 0, page_zero_bytes);
	
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	// PJ3
	// 잘못된 page가 들어왔으면 그냥 예외 처리
	if (page == NULL) {
		return false;
	}
	
	struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)page->uninit.aux;
	
	// swap out 되기 전에 이 page가 write 등 수정되었었다면, 그러니까 dirty bit를 조사하고
	// 수정되었다면 디스크에도 반영해준다.
	// 그리고 이제 반영이 되었으니 더 이상 더럽혀져 있지 않다는 의미로 0을 넣어준다. 이제 다른 프로세스 등이 여기에 접근해서 write할 수 있을 것 같다.
	// 추가로 애초에 write 등을 하면 하드웨어가 dirty bit을 1로 설정해둔다. 0으로 바꾸는 건 OS의 몫이다.
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		
		lock_acquire(&file_lock);
		file_write_at(aux->mapped_file, page->va, aux->page_read_bytes, aux->ofs);
		lock_release(&file_lock);
		
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	
	// 파일 테이블에서 연결을 끊는다.
	// 물리 메모리에서의 맵핑을 없게 만든다.
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	// PJ3
	// free(file_page->aux);
	free(page->frame);
}

void *undo_mmap(void *initial_addr, void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;
	
	while (initial_addr < addr) {
		page = spt_find_page(spt, addr);
		spt_remove_page(spt, page);
		// pml4_clear_page(&thread_current()->pml4, page->va);
		initial_addr += PGSIZE;
	}
	
	return NULL;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t read_bytes, int writable, struct file *file, off_t offset) {
	void *initial_addr = addr;
	
	// PJ3
	while (read_bytes > 0) {
		
		if (spt_find_page(&thread_current()->spt, addr)) {
			undo_mmap(initial_addr, addr);
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
			undo_mmap(initial_addr, addr);
		}
		read_bytes -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	
	return initial_addr;
}

// PJ3
void do_munmap(void *addr)
{
	struct thread *curr = thread_current();
	struct page *page;

	if ((page = spt_find_page(&curr->spt, addr)) == NULL) {
		return;
	}

	struct file *file = ((struct aux_for_lazy_load *)page->uninit.aux)->mapped_file;

	// SJ
	if (!file) {
		return;
	}

	while (page != NULL && page_get_type(page) == VM_FILE) {
		if (pml4_is_dirty(curr->pml4, page->va))
		{
			struct aux_for_lazy_load *aux = page->uninit.aux;
			
			lock_acquire(&file_lock);
			file_write_at(aux->mapped_file, addr, aux->page_read_bytes, aux->ofs);
			lock_release(&file_lock);
			
			pml4_set_dirty(curr->pml4, page->va, false);
			
		}

		pml4_clear_page(&curr->pml4, addr);
		addr += PGSIZE;
		page = spt_find_page(&curr->spt, addr);
	}
}