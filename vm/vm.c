/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/vaddr.h"
#include "include/userprog/process.h"
#include "threads/mmu.h"
#include "include/userprog/syscall.h"
#include "vm/anon.h"

struct list frame_table;
struct list_elem *recent_victim_elem;
struct lock frame_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	recent_victim_elem = list_begin(&frame_table);
	lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* 1. 새로운 page를 할당하고
 * 2. 각 페이지 타입에 맞는 initializer를 셋팅하고
 * 3. 유저 프로그램에게 다시 control을 넘긴다. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux) /* writable 왜 안씀? */
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* Create the page, fetch the initialier according to the VM type,
		 * and then create "uninit" page struct by calling uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			/* Fetch first, page_initialize may overwrite the values */
			uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
			break;
#ifdef EFILESYS /* For project 4 */
		case VM_PAGE_CACHE:
			uninit_new(page, pg_round_down(upage), init, type, aux, page_cache_initializer);
			break;
#endif
		}
		page->writable = writable;
		page->type = type;
		page->t = thread_current();

		/* Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = page_lookup(va);
	if (page)
	{
		return page;
	}
	return NULL; /* 사용자가 엉뚱한 va 요청했을 때 */
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = false;
	if (!hash_insert(&spt->vm, &page->hash_elem))
	{
		succ = true;
	}
	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	hash_delete(&spt->vm, &page->hash_elem);
	if (page->frame != NULL)
	{
		page->frame->page = NULL;
	}
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	/* TODO: The policy for eviction is up to you. */
	struct frame *victim_frame;
	struct page *victim_page;
	struct thread *frame_owner;
	struct list_elem *start = recent_victim_elem;

	for (recent_victim_elem = start;
		 recent_victim_elem != list_end(&frame_table);
		 recent_victim_elem = list_next(recent_victim_elem))
	{

		victim_frame = list_entry(recent_victim_elem, struct frame, frame_elem);
		if (victim_frame->page == NULL)
		{
			return victim_frame;
		}
		frame_owner = victim_frame->page->t;
		victim_page = victim_frame->page->va;
		if (pml4_is_accessed(frame_owner->pml4, victim_page))
		{
			pml4_set_accessed(frame_owner->pml4, victim_page, false);
		}
		else
		{
			return victim_frame;
		}
	}

	for (recent_victim_elem = list_begin(&frame_table);
		 recent_victim_elem != start;
		 recent_victim_elem = list_next(recent_victim_elem))
	{

		victim_frame = list_entry(recent_victim_elem, struct frame, frame_elem);
		if (victim_frame->page == NULL)
		{
			return victim_frame;
		}
		frame_owner = victim_frame->page->t;
		victim_page = victim_frame->page->va;
		if (pml4_is_accessed(frame_owner->pml4, victim_page))
		{
			pml4_set_accessed(frame_owner->pml4, victim_page, false);
		}
		else
		{
			return victim_frame;
		}
	}

	recent_victim_elem = list_begin(&frame_table);
	victim_frame = list_entry(recent_victim_elem, struct frame, frame_elem);
	return victim_frame;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */
	ASSERT(victim != NULL);
	if (victim->page != NULL)
	{
		if (swap_out(victim->page) == false)
		{
			PANIC("Swap out failed.");
		}
	}

	return victim;
}

/* palloc() and get frame. If there is no available page(frame), evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* palloc()을 수행하고 frame을 얻는다.
 * 만약 가용한 페이지가 없으면 페이지를 제거하고 반환한다.
 * 해당 함수는 항상 valid한 주소를 반환해야한다.
 * (user pool 메모리가 가득찬 경우,
 * 프레임을 제거해서 가용한 메모리 공간을 확보해야한다.) */
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;

	void *kva = palloc_get_page(PAL_USER);

	if (kva == NULL)
	{
		frame = vm_evict_frame();
		if (frame->page != NULL)
		{
			frame->page->frame = NULL;
			frame->page = NULL;
		}
	}
	else
	{
		frame = malloc(sizeof(struct frame));
		if (frame == NULL)
		{
			PANIC("todo: handle case when malloc fails.");
		}
		frame->kva = kva;
		frame->page = NULL;
		list_push_back(&frame_table, &frame->frame_elem);
	}

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth(void *addr UNUSED)
{
	if (vm_alloc_page(VM_MARKER_0 | VM_ANON, addr, true))
	{
		thread_current()->stack_bottom -= PGSIZE;
		return true;
	}

	return false;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{

	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* Validate the fault */
	if (!addr || is_kernel_vaddr(addr) || !not_present)
	{
		return false;
	}

	page = spt_find_page(spt, addr);

	if (!page)
	{
		if (addr >= USER_STACK - (1 << 20) && USER_STACK > addr && addr >= f->rsp - 8 && addr < thread_current()->stack_bottom)
		{
			void *fpage = thread_current()->stack_bottom - PGSIZE;
			if (vm_stack_growth(fpage))
			{
				page = spt_find_page(spt, fpage);
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claims the page to allocate va */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = spt_find_page(&thread_current()->spt, va);

	if (!page)
	{
		return false;
	}
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
/* 페이지를 요청하고 MMU를 세팅한다. */
static bool
vm_do_claim_page(struct page *page)
{
	struct thread *curr_thread = page->t;
	if (!page || !is_user_vaddr(page->va))
	{
		return false;
	}
	lock_acquire(&frame_lock);
	struct frame *frame = vm_get_frame();
	lock_release(&frame_lock);

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* 페이지의 VA를 프레임의 PA에 매핑하기 위해 PTE insert */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
	{
		return false;
	}
	pml4_set_accessed(curr_thread->pml4, page->va, true);

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->vm, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	struct page *parent_page;
	struct thread *child_thread = thread_current();
	bool success = false;

	hash_first(&i, &src->vm);
	while (hash_next(&i))
	{
		parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);

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

void *
page_destroy(struct hash_elem *hash_elem, void *aux UNUSED)
{
	struct page *page = hash_entry(hash_elem, struct page, hash_elem);
	vm_dealloc_page(page);
}

static void spt_destructor(struct hash_elem *e, void *aux)
{
	const struct page *page = hash_entry(e, struct page, hash_elem);
	enum vm_type type = page->operations->type;
	struct thread *t = thread_current();
	ASSERT(page != NULL);

	if (type == VM_FILE)
	{
		if (page->writable == true)
		{
			if (pml4_is_dirty(t->pml4, page->va))
			{
				struct aux_for_lazy_load *aux = page->uninit.aux;
				pml4_set_dirty(t->pml4, page->va, false);
			}
		}
	}
	else if (type == VM_ANON)
	{
		struct uninit_page *uninit_page = &page->uninit;
		int page_no = uninit_page->swap_index;

		if (page_no != -1)
		{
			bitmap_set(swap_table, page_no, false);
		}
	}

	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* Destroy all the supplemental_page_table hold by thread and
	 * writeback all the modified contents to the storage. */
	hash_destroy(&spt->vm, spt_destructor);
}
