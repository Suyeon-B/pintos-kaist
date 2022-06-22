/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "include/lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

// PJ3
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; // 8
static struct lock swap_lock;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// PJ3
	swap_disk = disk_get(1, 1);		// (1, 1)로 들어가면 swap을 위한 disk를 가져온다.
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;		// disk에 들어갈 수 있는 최대 page의 갯수이다. disk_size는 sector의 갯수를 반환하고, 이를 8로 나누면, swap_table에 담을 수 있는 page의 갯수가 나온다. 찍어보면 1008이 나오는데, 그러면 4KB * 1008해서 총 4MB 정도를 담을 수 있을 것 같다.
	swap_table = bitmap_create(swap_size);		// bitmap 자료구조로 swap_table을 관리하겠다.
	
	// lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// PJ3
	// struct uninit_page *uninit = &page->uninit;
	// memset(uninit, 0, sizeof(struct uninit_page));
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = -1;
	
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	
	// PJ3
	int page_no = anon_page->swap_index;
	
	// swap_table에 진짜 있는지 체크한다. swap out에서 true로 설정해주었다.
	// 즉 false이면 swap table에 없다는 의미가 된다. 물리 메모리에 있거나, swap table 혹은 물리 메모리에 둘 다 없거나.

	bool result = bitmap_test(swap_table, page_no);

	if (result == false) {
		return false;
	}
	
	// 물리 프레임에 swap table에 있던 데이터를 쓴다.
	for (int i = 0; i < SECTORS_PER_PAGE; ++i) {

		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);

	}
	
	// 이제 물리 메모리에 올라간다는 의미로 false를 해준다. 즉 swap table에 없다는 의미이다.

	bitmap_set(swap_table, page_no, false);

	
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	// PJ3
	// 비트맵을 순회해 first fit 방식으로 순회해서, 빈 swap_slot, 즉 비트가 0인 swap_slot을 찾는다.
	// page의 frame->kva의 데이터들을 disk에 쓸 수 있도록, 빈 swap_slot을 찾는다.
	// 8개의 섹터당 1개의 page 단위로, 최대 1008번 순회해서 page가 들어갈 빈 swap_slot의 index를 반환한다? 페이지 단위로 묶여있나?
	// swap_table에서, 0번째부터 시작해서 1개의 연속적인 8개 비트를 통째로 묶어서 탐색한다. 그리고 false, 즉 빈 swap_lost을 찾아 그것의 page_no을 반환한다.

	int page_no = bitmap_scan(swap_table, 0, 1, false);

	
	// page_no가 최대 1008번 정도인데 왜 이렇게 큰 수가 배정되어 있지?
	if (page_no == BITMAP_ERROR) {
		return false;
	}
	
	// 왜 ++i 되어 있었지? 난 i++ 할랭.
	// page_no은 파이썬의 리스트의 인덱스처럼 1이 아니라 0부터 시작한다.
	// 섹터 단위로 데이터를 디스크에 write한다.
	// disk sector            :   1 2 3 4 5 6 7 8   910111213141516  1718192021222324
	// each page              : | 1 2 3 4 5 6 7 8 | 1 2 3 4 5 6 7 8 | 1 2 3 4 5 6 7 8 | 
	// page_no & swap slot    :   0                 1                 2
	// 각 페이지의 한 섹터 단위로, 디스크의 섹터에 write한다.
	for (int i = 0; i < SECTORS_PER_PAGE; ++i) {

		disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);

	}
	
	// 이제 swap_table에 이 page가 swap out 되었으므로, 그 swap slot은 빈 것이 아니다. 따라서 이건 빈 swap slot으로 참조되지 않을 것이다.

	bitmap_set(swap_table, page_no, true);

	
	pml4_clear_page(thread_current()->pml4, page->va);		// 이제 물리 메모리에서 내려가게 되니 끊어준다.
	
	// swap table에서 이 page가 어디에 저장이 되는지, 자신의 struct page의 anon_page에 저장한다.
	anon_page->swap_index = page_no;
	
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// PJ3
	// free(anon_page->aux);
	// palloc_free_page(page->frame->kva);
	free(page->frame);
}
