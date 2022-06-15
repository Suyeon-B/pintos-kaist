/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/vaddr.h"
#include "include/userprog/process.h"
#include "threads/mmu.h"

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
	bool success = false;
	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page)); /* 수상함 */
		bool success = true;

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

		/* TODO: Insert the page into the spt. */
		success = spt_insert_page(spt, page);

		return success;
	}
err:
	return success;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	/* TODO: Fill this function. */
	/* pg_round_down()으로 vaddr의 페이지 번호를 얻음 */
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
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
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
	/* 만약 가용한 프레임이 없으면 제거하고 반환한다. */
	/* 고민한 점
	 * 	- malloc OR palloc */
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;

	// if (!frame->kva)
	// {
	// 	// PANIC("vm_get_frame에서 palloc 실패!");
	// 	// return vm_evict_frame(); /* 구현 전 */
	// }
	/* swap in swap out */
	/* frame 할당 실패 시 */
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
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
	/* vaild 체크 후 invalid하다면 Kill */
	/* 여기서 바로 process_exit->supplemental_page_table_kill로 이어짐 */
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = NULL;
	if (!addr || !user)
	{
		return false;
	}
	page = spt_find_page(spt, addr);
	return false;
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

	/* 주소 잘못들어오는 거 debugging 중 */
	if (!page)
	{
		return false; /* 수상함 - 예외처리하기 */
	}
	return vm_do_claim_page(page);
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
/* 페이지를 요청하고 MMU를 세팅한다. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO
	 * : 페이지의 VA를 프레임의 PA에 매핑하기 위해
	 *   PTE insert */

	/* 주소 잘못들어오는 거 debugging 중 */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
	{ /* initialize writable 초기화 했는지 확인하기 */
		return false;
	}
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
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
