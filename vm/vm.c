/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

// PJ3
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "string.h"
#include "include/lib/user/syscall.h"

// PJ3

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

// PJ3
// Helpers for hash table
unsigned
page_hash (const struct hash_elem *page_elem, void *aux UNUSED) {
	const struct page *page = hash_entry (page_elem, struct page, page_elem);
	
	return hash_bytes(&page->va, sizeof page->va);
}

bool
page_less (const struct hash_elem *page_elem_a, const struct hash_elem *page_elem_b, void *aux UNUSED) {
	const struct page *page_a = hash_entry(page_elem_a, struct page, page_elem);
	const struct page *page_b = hash_entry(page_elem_b, struct page, page_elem);
	
	return page_a->va < page_b->va;
}

struct page *
page_lookup (const void *va) {
	struct page page;
	struct hash_elem *page_elem;
	
	// printf("\n\n ### va : %p###\n\n", va); /* 지워 */
	page.va = va;
	page_elem = hash_find(&thread_current()->spt.vm, &page.page_elem);
	
	return page_elem != NULL ? hash_entry(page_elem, struct page, page_elem) : NULL;
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	// printf("\n\n ### vm_alloc_page_with_initializer ### \n\n");
	ASSERT (VM_TYPE(type) != VM_UNINIT)
	bool success = false;
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		
		// PJ3
		// 맨 처음엔 uninit 형태로 생기고, vm_type에 따라 
		struct page *page = (struct page *) malloc(sizeof (struct page));
		
		switch (VM_TYPE(type)) {	
			case VM_ANON:
				uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
				break;
		}
		page->writable = writable;

		/* Insert the page into the spt. */
		success = spt_insert_page(spt, page);	

		return success;
	}
err:
	return success;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	// PJ3
	page = page_lookup(pg_round_down(va));
	
	if (page == NULL) {
		// printf("\n\n ### page : hi ### \n\n"); /* 지워 */
		return NULL;
	}
	
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	// PJ3
	
	if (hash_insert(&spt->vm, &page->page_elem) == NULL) {
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	// PJ3
	hash_delete(&spt->vm, &page->page_elem);
	
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	// PJ3
	frame = (struct frame *) malloc(sizeof (struct frame));
	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;

	// if (frame->kva == NULL) {
	// 	// PANIC("##### TODO, Hi! page fault occurs. #####\n");
	// 	frame = vm_evict_frame();
	// 	frame->page = NULL;
	// 	return frame;
	// }

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr UNUSED) {
	// PJ3
	if (vm_alloc_page(VM_MARKER_0 | VM_ANON, addr, true)) {
		thread_current()->stack_bottom -= PGSIZE;
		return true;
		// printf("\n\n ### vm_stack_growth - fail 1 ### \n\n");
	}
	
	return false;
	
	// if (!vm_claim_page(pg_round_down(addr))) {
	// 	printf("\n\n ### vm_stack_growth - fail 2 ### \n\n");
	// 	return;
	// }
	// printf("\n\n ### vm_stack_growth - 3 ### \n\n");
	// printf("\n\n ### new stack_bottom : %p ### \n\n", pg_round_down(addr));
	
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
			
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	
	// PJ3
	if (addr == NULL || is_kernel_vaddr(addr) || not_present == false) {
		// exit(-1);
		return false;
	}
	
	page = spt_find_page(spt, addr);
	
	if (page == NULL) {
		if (addr >= USER_STACK - (1 << 20) && USER_STACK > addr && addr == f->rsp - 8 && addr < thread_current()->stack_bottom) {
			void *fpage = thread_current()->stack_bottom - PGSIZE;
			
			if (vm_stack_growth(fpage)) {
				page = spt_find_page(spt, fpage);
				
			} else {
				return false;
				
			}
		} else {
			return false;
		}
	}
	
	// printf("\n\n ### vm_try_handle_fault ### \n\n"); /* 지워 */
	return vm_do_claim_page(page);
	
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	// PJ3
	// page = (struct page *) pg_round_down(va);
	// GitBook의 Hash Table 참고
	// page_lookup은 빈 struct page를 선언하여 Hash Table 안의 page를 찾아 반환한다.
	// printf("\n\n ### vm_claim_page ### \n\n"); /* 지워 */
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	
	if (frame != NULL) {
		list_push_back(&frame_table, &frame->frame_elem);
	}
	
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// PJ3
	// uint64_t *pte = pml4e_walk (thread_current()->pml4, (uint64_t) page->va, 1);
	// bool rw = is_writable(pte); 
	
	// pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
	
	// 한양대 PPT 64 페이지 참고
	// 왜 install_page하면 안될까?
	// if (pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
	// 	return false;
	// }
	
	if (!install_page(page->va, frame->kva, page->writable)) {
		return false;
	}
	
	// if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
	// 	return false;
	// }
	
	// printf("\n\n ### vm_do_claim_page - frame->kva : %p ### \n\n", frame->kva); /* 지워 */
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// PJ3
	hash_init(&spt->vm, page_hash, page_less, NULL);
}	

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	// PJ3
	struct hash_iterator i;
	struct page *parent_page;
	struct thread *child_thread = thread_current();
	bool success = false;
	
	// printf("\n\n ### %p ### \n\n", src->vm);
	// printf("\n\n ### %p ### \n\n", &(src->vm));
	hash_first(&i, &src->vm);
	while (hash_next(&i))
	{
		parent_page = hash_entry(hash_cur(&i), struct page, page_elem);

		success = vm_alloc_page_with_initializer(parent_page->uninit.type,
												 parent_page->va,
												 parent_page->writable,
												 parent_page->uninit.init,
												 parent_page->uninit.aux);
		struct page *child_page = spt_find_page(&child_thread->spt, parent_page->va);

		/* anonymous page OR file backed page */
		if (parent_page->frame)
		{
			success = vm_do_claim_page(child_page);
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return success;
}

// PJ3
static void
page_destroy(struct hash_elem *page_elem, void *aux) {
	// if (page_elem == NULL) {
	// 	return;
	// }
	struct page *page = hash_entry (page_elem, struct page, page_elem);
	vm_dealloc_page(page);
	// free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// PJ3
	// hash_apply(spt->vm, page_destroy);
	hash_destroy(&spt->vm, page_destroy);
}
