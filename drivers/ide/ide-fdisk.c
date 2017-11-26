#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/genhd.h>
#include <linux/interrupt.h>
#include <linux/acs_nas.h>
#include <linux/hotswap.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>


#define MAX_PART_NUM    4
#define PART_VAR        1
#define PART_SWAP       2
#define PART_NVR        3
#define PART_DATA       4

#define geo_sectors     start_cyl
#define geo_heads       size_cyl
#define geo_cylinders   start_sec

struct hd_big_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned int cylinders;
	unsigned long start;
};

struct my_partition {
        unsigned char   boot;
        unsigned char   begin_head;
        unsigned char   begin_sector;
        unsigned char   begin_cyl;
        unsigned char   sys;
        unsigned char   end_head;
        unsigned char   end_sector;
        unsigned char   end_cyl;
        unsigned char   start[4];
        unsigned char   size[4];
};
struct part {
        unsigned int    start_cyl;
        unsigned int    size_cyl;
        unsigned int    start_sec;
        unsigned int    size_sec;
        int             state;
};


#define	TRUE	1
#define	FALSE	0
#ifdef	NAS_BOOT_SECTOR
extern unsigned char boot_sec_mod[64];
#endif
//extern unsigned char get_nvram_disk_size(void);

struct hd_big_geometry	geometry;
unsigned int	sectors, heads;

#define set_hsc(h,s,c,sector) { \
				s = sector % sectors + 1;	\
				sector /= sectors;	\
				h = sector % heads;	\
				sector /= heads;	\
				c = sector & 0xff;	\
				s |= (sector >> 2) & 0xc0;	\
			}

void set_start_sect(struct my_partition *p, unsigned int start_sect);
//unsigned int get_start_sect(struct my_partition *p);
unsigned int get_nr_sects(struct my_partition *p);
int create_boot_sector(ide_drive_t *drive, struct part *part_table, unsigned char minor, unsigned char *buf);
int add_partition_table(unsigned int i,	struct part *part_table);
int set_raid_partition(unsigned char *buf, unsigned char minor);

void
set_start_sect(struct my_partition *p, unsigned int start_sect) 
{
	p->start[0] = (start_sect & 0xff);
	p->start[1] = ((start_sect >> 8) & 0xff);
	p->start[2] = ((start_sect >> 16) & 0xff);
	p->start[3] = ((start_sect >> 24) & 0xff);
}

static void
set_nr_sects(struct my_partition *p, unsigned int sects) 
{
	p->size[0] = (sects & 0xff);
	p->size[1] = ((sects >> 8) & 0xff);
	p->size[2] = ((sects >> 16) & 0xff);
	p->size[3] = ((sects >> 24) & 0xff);
}

int my_fdisk(ide_drive_t *, unsigned char);
int set_raid_disk(dev_t dev)
{
	unsigned char disknum, prm_scd, mst_slv;
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	int ret = -1;

	disknum = get_disknum_by_dev(dev);
	get_disk_intf(disknum, &prm_scd, &mst_slv);
	
	hwif = &ide_hwifs[prm_scd];
	drive = &hwif->drives[mst_slv];
	ret = my_fdisk(drive, MINOR(dev));

	return ret;
}

void set_partition(struct my_partition *p, uint start, uint stop, int sysid)
{
	p->boot = 0;
	p->sys = sysid;
	set_start_sect(p, start);
	set_nr_sects(p, stop - start + 1);

	if (start/(geometry.sectors * geometry.heads) > 1023)
		start = geometry.heads * geometry.sectors * 1024 - 1;

	set_hsc(p->begin_head, p->begin_sector, p->begin_cyl, start);

	if (stop/(geometry.sectors * geometry.heads) > 1023)
		stop = geometry.heads * geometry.sectors * 1024 - 1;

	set_hsc(p->end_head, p->end_sector, p->end_cyl, stop);
}

int
create_boot_sector(ide_drive_t *drive, struct part *part_table, 
	unsigned char minor, unsigned char *buf)
{
	int	i, ret = 0;
	struct my_partition 	*n_p;
	unsigned int	start, stop;

#ifdef	ACS_DEBUG
	printk("create_boot_sector(): %s\n", drive->name);
#endif

	sectors = geometry.sectors; 
	heads = geometry.heads;

	for (i = 0; i < 512; i++)
		buf[i] = 0;

	for (i = 0; i < MAX_PART_NUM; i++) {
		n_p = (struct my_partition *)&buf[0x1BE + i * 16];	

		if (part_table[i+1].size_sec != 0) {
			start = part_table[i+1].start_sec;
			stop = part_table[i+1].start_sec + part_table[i+1].size_sec - 1;
			set_partition(n_p, start, stop, 0x83);
		}
		else
			set_partition(n_p, 0, 0, 0);
	}
	
	buf[0x1FE] = 0x55;
	buf[0x1FF] = 0xAA;
	return(ret);
}

int
set_raid_partition(unsigned char *buf, unsigned char minor)
{
	int	ret = 0;
	struct my_partition 	*n_p;

#ifdef	ACS_DEBUG
	printk("set_raid_partition(): minor = %d\n", minor);
#endif
	minor = minor % 64; 
	n_p = (struct my_partition *)&buf[0x1BE + (minor-1) * 16];	
	n_p->sys = LINUX_RAID_PARTITION;
	
	return(ret);
}

int cmp_boot_sector(unsigned char *buf_o, unsigned char *buf_n) 
{
	unsigned short i, j;
	struct my_partition     *n_p1,*n_p4;/* Holly 2006.08.07 */
	n_p1 = (struct my_partition *)&buf_o[0x1BE + 0 * 16];
        n_p4 = (struct my_partition *)&buf_o[0x1BE + 3 * 16];

	if ((buf_o[0x1FE] != 0x55) || (buf_n[0x1FF] != 0xAA)) {
		printk("cmp_boot_sector() 0x55AA\n");
		return(0);
	}

	for (i = 0; i < MAX_PART_NUM; i++) {
		for (j = 8; j < 16; j++) {
			if (buf_o[0x1BE +i*16+j] != buf_n[0x1BE +i*16+j]) {
				printk("cmp_boot_sector() i = %d, j = %d\n", i, j);
				return(0);
			}
		}
	}

	if(n_p1->sys != LINUX_RAID_PARTITION && n_p4->sys != LINUX_RAID_PARTITION) /* Holly 2006.08.07 */
                return(0);

	printk("cmp_boot_sector() OK\n");
	return(1);
}

#if	0
static dev_t get_dev_by_name(char *name)
{
	unsigned char disknum;
	dev_t dev_num;

	disknum = get_disk_num(name);
	if(disknum >= DISK_NUM)
		BUG_ON(1);

	switch	(disknum) {
	case 0:
		dev_num = MKDEV(3, 0);
		break;
	case 1:
		dev_num = MKDEV(3, 64);
		break;
	case 2:
		dev_num = MKDEV(22, 0);
		break;
	case 3:
		dev_num = MKDEV(22, 64);
		break;
	}
	
	return dev_num;
}
#endif

extern int bi_complete(struct bio *, unsigned int, int);

static int
rw_boot_sector(ide_drive_t *drive, unsigned char *buf, 
	unsigned char cmd, unsigned long start_sec)
{
        struct bio_vec *bio_vec;
        struct bio *bio;
        struct block_device *bdev;
        struct page *page;
        struct gendisk *disk;
        struct completion event;
        int ret = 0, part;

        if ((cmd != WIN_READ) && (cmd != WIN_WRITE)) {
                printk("rw_boot_sector(): cmd = 0x%x\n", cmd);
                goto no_bio;
        }

        bio = kmalloc(sizeof(struct bio), GFP_KERNEL);
        if (!bio) {
                printk(KERN_NOTICE "rw_boot_sector(): bio allocation failed\n");                goto no_bio;
        }

        bio_vec = kmalloc(sizeof(struct bio_vec), GFP_KERNEL);
        if (!bio_vec) {
                printk(KERN_NOTICE "rw_boot_sector(): bio_vec allocation failed\n");
                goto no_bvl;
        }
        memset(bio_vec, 0, sizeof(struct bio_vec));

        bdev = bdget(get_dev_by_name(drive->name));
        disk = get_gendisk(bdev->bd_dev, &part);
        bdev->bd_disk = disk;
        bdev->bd_contains = bdev;

        page = alloc_page(GFP_KERNEL);
        if (!page) {
                printk(KERN_NOTICE "rw_boot_sector(): page allocation failed\n");
                goto no_page;
        }

        if (cmd == WIN_READ) {
                ClearPageUptodate(page);
                ClearPageError(page);
                memset(page_address(page), 0, PAGE_SIZE);
        }
        if (cmd == WIN_WRITE)
                memcpy(page_address(page), buf, DEFAULT_SECTOR_SIZE);

        bio_init(bio);

        bio->bi_vcnt = 1;
        bio->bi_idx = 0;
        bio->bi_max_vecs = 1;
        bio->bi_io_vec = bio_vec;
        bio_vec->bv_page = page;
        bio_vec->bv_len = DEFAULT_SECTOR_SIZE;
        bio_vec->bv_offset = 0;

        bio->bi_size = DEFAULT_SECTOR_SIZE;
        bio->bi_bdev = bdev;
        bio->bi_sector = 0;
        init_completion(&event);
        bio->bi_private = &event;
        bio->bi_end_io = bi_complete;

        if (cmd == WIN_READ)
                submit_bio(READ, bio);
        else if (cmd == WIN_WRITE)
                submit_bio(WRITE, bio);
        wait_for_completion(&event);
        ret = test_bit(BIO_UPTODATE, &bio->bi_flags) ? 0 : -EIO;
        if(ret!= 0 )  /*robi,06.08.15*/
        {
                //set_disk_fail(sd->minor/16);
                goto fail;
        }
        if (cmd == WIN_READ) {
                memset(buf, 0, DEFAULT_SECTOR_SIZE);
                memcpy(buf, page_address(page), DEFAULT_SECTOR_SIZE);
        }

fail:
        __free_page(page);
        bdput(bdev);
        kfree(bio);

        return ret;

no_page:
        kfree(bio_vec);
no_bvl:
        kfree(bio);
no_bio:
        return -ENOMEM;
}

int
add_partition_table(unsigned int i, struct part *part_table)
{
	unsigned int	sectors, heads, cylinders, j, overlap = FALSE;
	struct part	*p_new = &part_table[i], *p;

	if (p_new->size_cyl == 0) {
		p_new->start_sec = 0;
		p_new->size_sec = 0;
		return(0);
	}

	for (j = 1; j <= MAX_PART_NUM; j++) {
		if ((j == i) || (part_table[j].size_cyl == 0)) 
			continue;

		p = &part_table[j];

		if ((p->start_cyl <= p_new->start_cyl) && (p_new->start_cyl <= p->start_cyl + p->size_cyl - 1))
			overlap = TRUE;

		if ((p->start_cyl <= p_new->start_cyl + p_new->size_cyl - 1) && (p_new->start_cyl + p_new->size_cyl -1 <= p->start_cyl + p->size_cyl - 1))
			overlap = TRUE;

		if (overlap == TRUE) {
			printk("add_partition_table(): Partition %d overlaps with partition %d.", i, j);
			return(-1);
		}
	}
	
	sectors = part_table[0].geo_sectors;
	heads = part_table[0].geo_heads;
	cylinders = part_table[0].geo_cylinders;

	if (p_new->start_cyl > cylinders) {
		printk("add_partition_table(): Out of range! Start cylinder(%d) > %d", p_new->start_cyl, cylinders);
		return(-1);
	}

	if (p_new->start_cyl + p_new->size_cyl - 1 > cylinders) {
		printk("add_partition_table(): Out of range! The last cylinder(%d) > %d", p_new->start_cyl + p_new->size_cyl - 1, cylinders);
		return(-1);
	}

	p_new->start_sec = (p_new->start_cyl-1) * sectors * heads;
	p_new->size_sec = p_new->size_cyl * sectors * heads;

	if (p_new->start_sec == 0) {
		p_new->size_sec -= sectors;
		p_new->start_sec = sectors;
	}

	p_new->state = 0;

	return(0);
}

extern char * get_sb_info(int hd_index, unsigned long sectors, unsigned long *sb_offset, unsigned long *length);
extern int analyze_sb(char *buf);
extern int md_current_instances(void);
//unsigned char obuf[4096];

int
my_fdisk(ide_drive_t *drive, unsigned char minor)
{
	struct part p_table[MAX_PART_NUM+1];
	int sectors, heads, cyls, ret = 0, o_boot_sec_ok = 0;
	unsigned char part_size = 0, buf_o[512], buf_n[512];
	unsigned long long capacity;

	sectors = 63;	
	heads = 16;	
	capacity = (unsigned long long)drive->capacity64 - drive->sect0;
        cyls =((unsigned int)capacity) / (sectors * heads) ;

	geometry.sectors = drive->bios_sect;	
	geometry.heads = drive->bios_head;	
	geometry.cylinders = cyls;	

	p_table[0].geo_sectors = geometry.sectors;	
	p_table[0].geo_heads = geometry.heads;	
	p_table[0].geo_cylinders = geometry.cylinders;	

	printk(KERN_EMERG"sector =%d heads =%d cyls=%d\n",sectors,heads,cyls);
	/* log */
	p_table[1].start_cyl = 16 * 2048 / (sectors * heads) + 1;
	p_table[1].size_cyl = 1024 * 2048 / (sectors * heads) + 1; /* 1024M */
	/* swap */
	p_table[2].start_cyl = p_table[1].start_cyl + p_table[1].size_cyl;
	p_table[2].size_cyl = 2048 * 2048 / (sectors * heads) + 1; /* 2048MB */

	/* NVRAM */
	p_table[3].start_cyl = p_table[2].start_cyl + p_table[2].size_cyl;
	p_table[3].size_cyl = 256 * 2048 / (sectors * heads) + 1; /* 256MB */
	
	/* data */
	p_table[4].start_cyl = p_table[3].start_cyl + p_table[3].size_cyl;
	//accusys_get_nvram_disk_size(&part_size);
	if (part_size == 32)
		p_table[4].size_cyl = 512 * 2048 / (sectors * heads) + 1; /*  512 MB */
	else if (part_size == 64)
		p_table[4].size_cyl = 256 * 2048 / (sectors * heads) + 1; /*  256 MB */
	else if ((part_size == 1) || (part_size == 2) || (part_size == 4) || (part_size == 8) || (part_size == 16))
		p_table[4].size_cyl = part_size * 1024 * 2048 / (sectors * heads) + 1; /*  part_size 1 GB */
	else
		p_table[4].size_cyl = cyls - p_table[4].start_cyl + 1; 

	ret  = add_partition_table(1, p_table);
	ret |= add_partition_table(2, p_table);
	ret |= add_partition_table(3, p_table);
	ret |= add_partition_table(4, p_table);

	ret |= create_boot_sector(drive, p_table, minor, buf_n);
	if (ret != 0){
		return(ret);
	}

	if ((ret = rw_boot_sector(drive, buf_o, WIN_READ, 0)) != 0){
		return(ret);
	}

	if (minor == 0) {
		o_boot_sec_ok = cmp_boot_sector(buf_o, buf_n);

		if (md_current_instances()) {
			if ((ret = rw_boot_sector(drive, buf_n, WIN_WRITE, 0)) != 0){
				return(ret);
			}
		} else {
			if (!o_boot_sec_ok) {
				if ((ret = rw_boot_sector(drive, buf_n, WIN_WRITE, 0)) != 0){
					return(ret);
				}
			}
		}
	} else {
		set_raid_partition(buf_o, minor);
		if ((ret = rw_boot_sector(drive, buf_o, WIN_WRITE, 0)) != 0)
			return(ret);
	}

	return(ret);
}

EXPORT_SYMBOL(set_raid_disk);
EXPORT_SYMBOL(my_fdisk);
