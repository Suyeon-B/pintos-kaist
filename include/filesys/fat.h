#ifndef FILESYS_FAT_H
#define FILESYS_FAT_H

#include "devices/disk.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t cluster_t; /* Index of a cluster within FAT. */

#define FAT_MAGIC 0xEB3C9000 /* MAGIC string to identify FAT disk */
#define EOChain 0x0FFFFFFF   /* End of cluster chain */

/* Sectors of FAT information. */
#define SECTORS_PER_CLUSTER 1 /* Number of sectors per cluster */
#define FAT_BOOT_SECTOR 0     /* FAT boot sector. */
#define ROOT_DIR_CLUSTER 1    /* Cluster for the root directory */

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot
{
    unsigned int magic;
    unsigned int sectors_per_cluster; /* Fixed to 1 */
    unsigned int total_sectors;
    unsigned int fat_start;   /* FAT table의 첫번째 idx */
    unsigned int fat_sectors; /* Size of FAT in sectors. */
    unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs
{
    struct fat_boot bs;       // Boot sector
    unsigned int *fat;        // 실제 File Allocation Table
    unsigned int fat_length;  // FAT 테이블의 인덱스 개수
    disk_sector_t data_start; // 데이터 블록 영역의 시작 클러스터 번호
    cluster_t last_clst;
    // struct lock write_lock;
};

static struct fat_fs *fat_fs;
struct bitmap *fat_bitmap;

void fat_init(void);
void fat_open(void);
void fat_close(void);
void fat_create(void);
void fat_close(void);

cluster_t fat_create_chain(
    cluster_t clst /* Cluster # to stretch, 0: Create a new chain */
);
void fat_remove_chain(
    cluster_t clst, /* Cluster # to be removed */
    cluster_t pclst /* Previous cluster of clst, 0: clst is the start of chain */
);
cluster_t fat_get(cluster_t clst);
void fat_put(cluster_t clst, cluster_t val);
disk_sector_t cluster_to_sector(cluster_t clst);

#endif /* filesys/fat.h */
