#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Project 4 -------------------------- */
#include "include/lib/kernel/bitmap.h"

void fat_boot_create(void);
void fat_fs_init(void);

void fat_init(void)
{
	/* FAT 담을 공간을 할당한다. */
	fat_fs = calloc(1, sizeof(struct fat_fs));
	if (fat_fs == NULL)
		PANIC("FAT init failed");

	/* Read boot sector from the disk
	 * booting 정보를 bounce에 read 한뒤,
	 * fat_fs->bs에 memcpy
	 * 이후 bounce는 할당해제 */
	unsigned int *bounce = malloc(DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC("FAT init failed");
	disk_read(filesys_disk, FAT_BOOT_SECTOR, bounce); /* boot sector read */
	memcpy(&fat_fs->bs, bounce, sizeof(fat_fs->bs));
	free(bounce);

	/* Extract FAT info
	 * fat_fs->bs.magic를 FAT_MAGIC 으로
	 * fat_boot_create()를 통해 초기화 */
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create();
	fat_fs_init();

	/* for implement FAT */
	fat_bitmap = bitmap_create(fat_fs->fat_length);
}

void fat_open(void)
{
	fat_fs->fat = calloc(fat_fs->fat_length, sizeof(cluster_t));
	if (fat_fs->fat == NULL)
		PANIC("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *)fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof(fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof(cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++)
	{
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE)
		{
			disk_read(filesys_disk, fat_fs->bs.fat_start + i,
					  buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		}
		else
		{
			uint8_t *bounce = malloc(DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC("FAT load failed");
			disk_read(filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy(buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free(bounce);
		}
	}
}

void fat_close(void)
{
	// Write FAT boot sector
	uint8_t *bounce = calloc(1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC("FAT close failed");
	memcpy(bounce, &fat_fs->bs, sizeof(fat_fs->bs));
	disk_write(filesys_disk, FAT_BOOT_SECTOR, bounce);
	free(bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *)fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof(fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof(cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++)
	{
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE)
		{
			disk_write(filesys_disk, fat_fs->bs.fat_start + i,
					   buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		}
		else
		{
			bounce = calloc(1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC("FAT close failed");
			memcpy(bounce, buffer + bytes_wrote, bytes_left);
			disk_write(filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free(bounce);
		}
	}
}

void fat_create(void)
{
	// Create FAT boot
	fat_boot_create();
	fat_fs_init();

	// Create FAT table
	fat_fs->fat = calloc(fat_fs->fat_length, sizeof(cluster_t));
	if (fat_fs->fat == NULL)
		PANIC("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put(ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc(1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC("FAT create failed due to OOM");
	disk_write(filesys_disk, cluster_to_sector(ROOT_DIR_CLUSTER), buf);
	free(buf);
}

void fat_boot_create(void)
{
	unsigned int fat_sectors =
		(disk_size(filesys_disk) - 1) / (DISK_SECTOR_SIZE / sizeof(cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
		.magic = FAT_MAGIC,
		.sectors_per_cluster = SECTORS_PER_CLUSTER,
		.total_sectors = disk_size(filesys_disk),
		.fat_start = 1,
		.fat_sectors = fat_sectors,
		.root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

/* FAT Filesystem(디스크)

	fat_start = 1
	total_sectors = 8
	fat_sectors = 3
	data_start = 4
	fat_length = 4 -> FAT table의 인덱스 개수

					+-------+-------+-------+-------+-------+-------+-------+-------+
					|Boot   |       FAT Table       |          Data Blocks          |
					|Sector |       |       |       |       |       |       |       |
					+-------+-------+-------+-------+-------+-------+-------+-------+
	sector :    		0       1       2       3       4       5       6       7
	cluster:                                    		1       2       3       4

*/
void fat_fs_init(void)
{
	/* data_start: DATA sector 시작 지점 */
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
	/* fat_length: FAT 테이블의 인덱스 개수 (파일 시스템에 얼마나 클러스터가 많은지) */
	fat_fs->fat_length = fat_fs->bs.total_sectors / SECTORS_PER_CLUSTER;
	// fat_fs->fat_length = fat_fs->bs.total_sectors - fat_fs->data_start;
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* FAT 비트맵에서 빈 클러스터를 가져온다. */
cluster_t get_empty_cluster()
{
	/* idx는 0부터 시작하지만, cluster는 1부터 시작 - why??? */
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false) + 1;
	if (clst == BITMAP_ERROR)
	{
		return 0;
	}
	else
	{
		return (cluster_t)clst;
	}
}

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t fat_create_chain(cluster_t clst)
{
	cluster_t new_clst = get_empty_cluster(); // fat bitmap에서 빈 클러스터를 찾는다.
	if (new_clst != 0)						  // 빈 클러스터가 있다면
	{
		fat_put(new_clst, EOChain); // 빈 클러스터에 EOF셋팅
		if (clst != 0)				// clst가 0이면(클러스터 시작) EOC셋팅만 하면 끝이고
		{
			fat_put(clst, new_clst); // clst가 0이 아니라면 clst에 방금 생성한 new_clst를 put
		}
	}
	return new_clst; // 빈 클러스터가 없다면 0을 그대로 리턴
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void fat_remove_chain(cluster_t clst, cluster_t pclst)
{
	/* Project 4-1: FAT */
	while (clst != EOChain)						 // clst(다음 클러스터)가 EOC가 아니면,
	{											 // 남은 클러스터가 있다는 의미
		bitmap_set(fat_bitmap, clst - 1, false); // clst부터 next clst로 옮기며 모두 삭제
		clst = fat_get(clst);					 // bitmap에 할당 여부를 0으로 리셋
	}
	if (pclst != 0)
	{
		fat_put(pclst, EOChain); // pclst를 클러스터의 새로운 끝으로 설정
	}
}

/* Update a value in the FAT table.
   fat 테이블에 해당 인덱스 값을 업데이트 */
void fat_put(cluster_t clst, cluster_t val)
{
	ASSERT(clst >= 1);
	if (!bitmap_test(fat_bitmap, clst - 1)) // 클러스터 값은 1부터 시작.
	{										// 해당 인덱스가 fat_bitmap에 들어있는지 체크.
		bitmap_mark(fat_bitmap, clst - 1);	// 인덱스가 없다면 비트맵에서 해당 인덱스에 true로 변경
	}
	fat_fs->fat[clst - 1] = val - 1; // fat 테이블에 인덱스 값 업데이트
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get(cluster_t clst)
{
	ASSERT(clst >= 1);
	/* 만약 클러스터 번호가 fat_length를 넘어가거나 fat 테이블에 들어있지 않다면 */
	if (clst > fat_fs->fat_length || !bitmap_test(fat_bitmap, clst - 1))
		return 0;
	/* fat 테이블에서 해당 인덱스에 들어있는 값을 반환 */
	return fat_fs->fat[clst - 1];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector(cluster_t clst)
{
	ASSERT(clst >= 1);
	/* N번 클러스터가 디스크 상의 몇 번째 섹터인지를 계산 */
	return fat_fs->data_start + (clst - 1) * SECTORS_PER_CLUSTER;
}
