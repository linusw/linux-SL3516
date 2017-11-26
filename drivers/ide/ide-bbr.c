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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/acs_nas.h>
#include <linux/bad_blk_remap.h>
#include <linux/hotswap.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#ifdef	BAD_BLK_REMAP
void remap_do_request(unsigned char prm_scd, unsigned char mst_slv)
{
        ide_drive_t *drive = &ide_hwifs[prm_scd].drives[mst_slv];
        struct request_queue *q = drive->queue;
        unsigned long flags;

        spin_lock_irqsave(&ide_lock, flags);

/*      if (test_bit(drive->queue.plugged)) {
                clean_bit(drive->queue.plugged);
        }
*/
        if (HWGROUP(drive)->busy) {
                HWGROUP(drive)->busy = 0;
                do_ide_request(q);
        }
        else {
                do_ide_request(q);
        }

        spin_unlock_irqrestore(&ide_lock, flags);
        printk(" end remap do request\n");
}

extern int bi_complete(struct bio*, unsigned int, int);
int rw_remap_sector(unsigned char disknum, unsigned char *buf,
        unsigned char cmd, unsigned long start_sec)
{
        struct bio_vec *bio_vec;
        struct bio *bio;
        struct block_device *bdev;
        struct page *page;
	ide_drive_t *drive;
        struct gendisk *disk;
        struct completion event;
        int ret = 0, part;
	unsigned char prm_scd, mst_slv;
        remap_descriptor_t *remap_descriptor = &(remap_infos[disknum].remap_descriptor);

        if ((cmd != WIN_READ) && (cmd != WIN_WRITE)) {
                printk("%s: cmd = 0x%x\n", __func__, cmd);
                goto no_bio;
        }
	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &ide_hwifs[prm_scd].drives[mst_slv];
	
        bio = kmalloc(sizeof(struct bio), GFP_KERNEL);
        if (!bio) {
                printk("%s: bio allocation failed\n", __func__);
		goto no_bio;
        }

        bio_vec = kmalloc(sizeof(struct bio_vec), GFP_KERNEL);
        if (!bio_vec) {
                printk("%s: bio_vec allocation failed\n", __func__);
                goto no_bvl;
        }
        memset(bio_vec, 0, sizeof(struct bio_vec));

        bdev = bdget(get_dev_by_name(drive->name));
        disk = get_gendisk(bdev->bd_dev, &part);
        bdev->bd_disk = disk;
        bdev->bd_contains = bdev;

        page = alloc_page(GFP_KERNEL);
        if (!page) {
                printk("%s: page allocation failed\n", __func__);
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
        bio->bi_sector = remap_descriptor->start_sec + start_sec;
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

#endif //BAD_BLK_REMAP
