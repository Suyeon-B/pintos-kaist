/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;
	memset(kva, 0, PGSIZE);

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct uninit_page *uninit_page = &page->uninit;

	int page_no = uninit_page->swap_index;

	if (bitmap_test(swap_table, page_no) == false)
	{
		return false;
	}

	for (int i = 0; i < SECTORS_PER_PAGE; ++i)
	{
		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}

	bitmap_set(swap_table, page_no, false);
	uninit_page->swap_index = -1;

	return true;
}
/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct uninit_page *uninit_page = &page->uninit;

	size_t page_no = bitmap_scan(swap_table, 0, 1, false);

	if (page_no == BITMAP_ERROR)
	{
		return false;
	}

	for (int i = 0; i < SECTORS_PER_PAGE; ++i)
	{
		disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	bitmap_set(swap_table, page_no, true);
	pml4_clear_page(page->t->pml4, page->va);

	uninit_page->swap_index = page_no;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
// 내꺼
static void
anon_destroy(struct page *page)
{
	// /* aux 할당 해제 */
	/* TODO : aux 할당 해제는 언제 해줄까? */
	// struct uninit_page *uninit UNUSED = &page->uninit;
	// struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)(uninit->aux);

	// free(aux);
	free(page->frame); /* frame 할당 해제 */
}

// static void
// anon_destroy(struct page *page)
// {
// 	// struct anon_page *anon_page = &page->anon;
// 	struct uninit_page *uninit UNUSED = &page->uninit;
// 	struct aux_for_lazy_load *aux = (struct aux_for_lazy_load *)(uninit->aux);

// 	free(aux);
// }