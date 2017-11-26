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
#include <linux/blkdev.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <asm/arch/sl2312.h>
#include <asm/arch/gemini_gpio.h>
#include <linux/acs_nas.h>
#include <linux/hotswap.h>
#include <linux/acs_char.h>	//add for ERROR_LOG

wait_queue_head_t drive_wqueue[DISK_NUM];


/*  Name:   		get_disk_intf
 *  Description:    	get disk interface from disk number	
 *  @disk_num:  disk number
 *  @prm_scd:  primary or secondary
 *	0 - primary, 1 - secondary
 *  @mst_slv:  master or slave
 *	0 - master, 1 - slave
 */
void get_disk_intf(unsigned char disk_num, 
	unsigned char *prm_scd, unsigned char *mst_slv)
{
	if(disk_num >= DISK_NUM) {
		BUG_ON(1);
		return;
	}

	switch (disk_num){
	case 0:
		*prm_scd = 0; *mst_slv = 0;
		break;
	case 1:
		*prm_scd = 0; *mst_slv = 1;
		break;
	case 2:
		*prm_scd = 1; *mst_slv = 0;
		break;
	case 3:
		*prm_scd = 1; *mst_slv = 1;
		break;
	}	

	return;
}

void set_disk_fail(unsigned char disknum)
{
        unsigned char access_led_pin = disknum_to_pin(disknum, 1);

	drive_stat[disknum] |= IDE_FAIL;

	gemini_gpio_pin_write(access_led_pin, 1, 0x08); //set dir to output
	gemini_gpio_pin_write(access_led_pin, 1, 0x14); //clear
	gemini_gpio_pin_write(access_led_pin, 1, 0x10); //turn on status LED
}

void ide_soft_reset(unsigned char);
void ide_hard_reset(unsigned char disknum)
{
        unsigned long reg;
	unsigned char prm_scd, mst_slv;

#ifdef	ACS_DEBUG
	acs_printk("%s: disknum=%d\n", __func__, disknum);
#endif

	get_disk_intf(disknum, &prm_scd, &mst_slv);	

	if (!prm_scd) {	//hda and hdb
        	reg = readl(IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
#ifdef	ACS_DEBUG
		acs_printk("%s: reg=0x%x\n", __func__, reg);
#endif
		reg |= RESET_IDE;
		//reg = RESET_IDE;
        	writel(reg, IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
	} else if (!mst_slv) {	//hdc
        	reg = readl(IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
#ifdef	ACS_DEBUG
		acs_printk("%s: reg=0x%x\n", __func__, reg);
#endif
		reg |= RESET_SATA1;
		//reg = RESET_SATA1;
        	writel(reg, IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
	} else {	//hdd
        	reg = readl(IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
#ifdef	ACS_DEBUG
		acs_printk("%s: reg=0x%x\n", __func__, reg);
#endif
		reg |= RESET_SATA0;
		//reg = RESET_SATA0;
        	writel(reg, IO_ADDRESS(SL2312_GLOBAL_BASE) + GLOBAL_RESET_REG);
	}

	msleep(100);
	if (!prm_scd && ide_hwifs[prm_scd].drives[!mst_slv].present)
		ide_soft_reset(prm_scd << 1 + (1 - mst_slv));

	return;
}

void ide_soft_reset(unsigned char disknum)
{
	unsigned char prm_scd, mst_slv;
	ide_hwif_t *hwif;
	ide_drive_t *drive;

	get_disk_intf(disknum, &prm_scd, &mst_slv);
	hwif = &ide_hwifs[prm_scd];
	drive = &hwif->drives[mst_slv];

       	if (hwif->tuneproc != NULL && drive->autotune == IDE_TUNE_AUTO)
                /* auto-tune PIO mode */
                hwif->tuneproc(drive, 255);

	if (drive->autotune != IDE_TUNE_DEFAULT && drive->autotune != IDE_TUNE_AUTO)
        	return;

        drive->nice1 = 1;

        if (hwif->ide_dma_check) {
               	hwif->ide_dma_off_quietly(drive);
                hwif->ide_dma_check(drive);
        }

	return;
}

//test
void get_ide_stat(unsigned char *stat)
{
        ide_drive_t *drive = &(ide_hwifs[0].drives[0]);	
	SELECT_DRIVE(drive);
        //ide_hwifs[0].OUTB(8, ide_hwifs[0].io_ports[IDE_CONTROL_OFFSET]);
        mdelay(20);	
	stat[0] = ide_hwifs[0].INB(ide_hwifs[0].io_ports[IDE_STATUS_OFFSET]);

        drive = &(ide_hwifs[0].drives[1]);	
	SELECT_DRIVE(drive);
        //ide_hwifs[0].OUTB(8, ide_hwifs[0].io_ports[IDE_CONTROL_OFFSET]);
        mdelay(20);	
	stat[1] = ide_hwifs[0].INB(ide_hwifs[0].io_ports[IDE_STATUS_OFFSET]);

        drive = &(ide_hwifs[1].drives[0]);	
	SELECT_DRIVE(drive);
        //ide_hwifs[1].OUTB(8, ide_hwifs[1].io_ports[IDE_CONTROL_OFFSET]);
        mdelay(20);	
	stat[2] = ide_hwifs[1].INB(ide_hwifs[1].io_ports[IDE_STATUS_OFFSET]);

        drive = &(ide_hwifs[1].drives[1]);	
	SELECT_DRIVE(drive);
        //ide_hwifs[1].OUTB(8, ide_hwifs[1].io_ports[IDE_CONTROL_OFFSET]);
        mdelay(20);	
	stat[3] = ide_hwifs[1].INB(ide_hwifs[1].io_ports[IDE_STATUS_OFFSET]);
}

extern int hwif_init(ide_hwif_t*, int);
extern void probe_hwif(ide_hwif_t*, int);
static int ide_remove_disk(unsigned char, unsigned int);

static int
ide_add_disk(unsigned char disknum)
{
        ide_hwif_t *hwif;
        ide_drive_t *drive;
	struct block_device *blk_dev;
        struct gendisk *disk;

	unsigned char prm_scd, mst_slv;
#ifdef  ACS_DEBUG
        acs_printk("%s: disknum=%d, pid=%d\n", __func__, disknum, current->pid);
#endif

	get_disk_intf(disknum, &prm_scd, &mst_slv);

        hwif = &ide_hwifs[prm_scd];
        if (hwif == NULL) {
		BUG_ON(1);
                return(-1);
        }

        drive = &hwif->drives[mst_slv];
        if (drive == NULL) {
		BUG_ON(1);
                return(-1);
        }

	memset(drive, 0, sizeof(ide_drive_t));
	drive->media                    = ide_disk;
        drive->select.all               = (mst_slv << 4) | 0xa0;
        drive->hwif                     = hwif;
        drive->ctl                      = 0x08;
        drive->ready_stat               = READY_STAT;
        drive->bad_wstat                = BAD_W_STAT;
        drive->special.b.recalibrate    = 1;
        drive->special.b.set_geometry   = 1;
        drive->name[0]                  = 'h';
        drive->name[1]                  = 'd';
        drive->name[2]                  = 'a' + disknum;
        drive->autotune                 = 1;
        drive->using_dma                = 1;
        drive->autodma                  = 1;
        drive->is_flash                 = 0;
        drive->vdma                     = 0;
        INIT_LIST_HEAD(&drive->list);
        sema_init(&drive->gendev_rel_sem, 0);

#if	0
	spin_lock_irq(&ide_lock);
        ide_hard_reset(disknum);
//test
	if (prm_scd == 0 && ide_hwifs[0].drives[!mst_slv].present)
		ide_soft_reset(disknum);
	spin_unlock_irq(&ide_lock);
#endif

        probe_hwif(hwif, mst_slv);
        if (!drive->present) {
#ifdef  ACS_DEBUG
                acs_printk("%s: !drive->present\n", __func__);
#endif
                goto add_fails;
        }

        hwif_init(hwif, mst_slv);
        if (!hwif->present) {
#ifdef  ACS_DEBUG
                acs_printk("%s: !hwif->present\n", __func__);
#endif
                goto add_fails;
        }
#ifdef  ACS_DEBUG
	acs_printk("%s: hwif_init OK\n", __func__);
#endif
        if (drive->present) {
#ifdef  ACS_DEBUG
		acs_printk("%s: device_register. prm_scd=%d, mst_slv=%d\n", __func__, prm_scd, mst_slv);
#endif
                device_register(&drive->gendev);
        }

        create_proc_ide_interfaces();

        if (my_fdisk(drive, 0)) {
#ifdef	ACS_DEBUG
		acs_printk("%s: fdisk fail!\n", __func__);
#endif
                goto add_fails;
        }

	disk = ((struct ide_disk_obj *)drive->driver_data)->disk;
        blk_dev = bdget_disk(disk, 0);
        blk_dev->bd_invalidated = 1;
        blkdev_get(blk_dev, FMODE_READ, 0);
        blkdev_put(blk_dev);

#ifdef  BAD_BLK_REMAP
        if (bad_blk_remap_init(disknum)) {
#ifdef	ACS_DEBUG
		acs_printk("%s: bad block remap init fail!\n", __func__);
#endif
                goto add_fails;
	}
#endif

        drive_stat[disknum] &= ~IDE_ADDING;
        hotswap_stat[disknum] = DISK_ADDED_OK;
        return(0);

add_fails:
        hotswap_stat[disknum] = DISK_ADDED_FAIL;
        ide_remove_disk(disknum, 0);
        drive_stat[disknum] &= ~IDE_ADDING;
	//set_disk_fail(disknum);
        drive_stat[disknum] |= IDE_FAIL;
#ifdef ORION_430ST
	unsigned char hdd_led_pin = dn_to_pin(disknum,1);
	
	if (hdd_led_pin == 1)
		write_kernellog("%d:SATA1_FAIL.",ERROR_LOG);
	else if(hdd_led_pin == 3)
		write_kernellog("%d:SATA2_FAIL.",ERROR_LOG);
#endif
        return(-1);
}

extern void elv_drain_elevator(request_queue_t *);

int ide_remove_rq(unsigned char disknum)
{
        ide_drive_t *drive;
        ide_hwif_t *hwif;
	unsigned char prm_scd, mst_slv;
#ifdef ACS_DEBUG
	unsigned int rq_count = 0;
        acs_printk("%s: disknum=%d, pid=%d\n", __func__, disknum, current->pid);
#endif

	get_disk_intf(disknum, &prm_scd, &mst_slv);

        hwif = &ide_hwifs[prm_scd];
        if (!hwif->present) {
                BUG_ON(1);
		return 0;
	}

        drive = &hwif->drives[mst_slv];
        if (!drive->present) {
                BUG_ON(1);
		return 0;
	}

        spin_lock_irq(&ide_lock);
        elv_drain_elevator(drive->queue);
        while (!list_empty(&drive->queue->queue_head)) {
                HWGROUP(drive)->rq = list_entry_rq(drive->
                        queue->queue_head.next);
                if (!(HWGROUP(drive)->rq->flags & REQ_STARTED)) {
                        HWGROUP(drive)->rq->flags |= REQ_STARTED;
                }
                spin_unlock_irq(&ide_lock);
                ide_end_request(drive, 0, 0);
                spin_lock_irq(&ide_lock);
#ifdef ACS_DEBUG
                rq_count++;
#endif
        }
        spin_unlock_irq(&ide_lock);
#ifdef ACS_DEBUG
        printk("%s: end %d requests\n", __func__, rq_count);
#endif

        return 0;
}

static int ide_remove_disk(unsigned char disknum, unsigned int lcd_flag)
{
        //struct gendisk *gd;
        ide_drive_t *drive;
        ide_hwif_t *hwif, *g;
        ide_hwgroup_t *hwgroup;
        int i = 0, ret = 0;
	unsigned char prm_scd, mst_slv;

#ifdef ACS_DEBUG
        printk("%s: disknum=%d, pid=%d\n", __func__, disknum, current->pid);
#endif
	get_disk_intf(disknum, &prm_scd, &mst_slv);

#ifdef ACS_DEBUG
        printk("%s: step 1\n", __func__);
#endif

        down(&ide_cfg_sem);
        //spin_lock_irq(&ide_lock);
        hwif = &ide_hwifs[prm_scd];
	drive = &hwif->drives[mst_slv];
        if (!hwif->present) {
#ifdef  ACS_DEBUG
                printk("%s: !present\n", __func__);
#endif
		return 0;
        }
#ifdef ACS_DEBUG
        printk("%s: step 2\n", __func__);
#endif
        if (!drive->present) {
		return 0;
	}
#ifdef ORION_430ST
	hdd_status_led(disknum, 1, 0);
#endif
	//hdd_status_led_ctl(disknum, 0); /* turn off HDD Status LED */

#ifdef ACS_DEBUG
        printk("%s: step 3\n", __func__);
#endif

	drive->wcache = 0;
        device_unregister(&(drive->gendev));
	down(&drive->gendev_rel_sem);
        spin_lock_irq(&ide_lock);
#ifdef ACS_DEBUG
        printk("%s: step 5\n", __func__);
#endif
	if(!(hwif->drives[!mst_slv].present))
        	hwif->present = 0;

#ifdef ACS_DEBUG
        printk("%s: step 6\n", __func__);
#endif
	spin_unlock_irq(&ide_lock);

	if (!(hwif->drives[!mst_slv].present))
		destroy_proc_ide_interface(hwif);

#ifdef ACS_DEBUG
        printk("%s: step 7\n", __func__);
#endif
        hwgroup = hwif->hwgroup;

        /*
         * make sure the other disk in the same hwif is not present
         */
	if(!(hwif->drives[!mst_slv].present)){
		/* make sure the irq is not shared with other hwif */
 		free_irq(hwif->irq, hwgroup);
#ifdef ACS_DEBUG
        	printk("%s: step 8\n", __func__);
#endif

		spin_lock_irq(&ide_lock);
        	/*
        	 * Remove us from the hwgroup, and free
        	 * the hwgroup if we were the only member
        	 */
	        if (hwif->next == hwif) {
	                BUG_ON(hwgroup->hwif != hwif);
	                kfree(hwgroup);
	        } else {
	                /* There is another interface in hwgroup.
	                 * Unlink us, and set hwgroup->drive and ->hwif to
	                 * something sane.
	                 */
	                g = hwgroup->hwif;
	                while (g->next != hwif)
	                        g = g->next;
	                g->next = hwif->next;
	                if (hwgroup->hwif == hwif) {
	                        /* Chose a random hwif for hwgroup->hwif.
	                         * It's guaranteed that there are no drives
	                         * left in the hwgroup.
	                         */
	                        BUG_ON(hwgroup->drive != NULL);
	                        hwgroup->hwif = g;
	                }
	                BUG_ON(hwgroup->hwif == hwif);
	        }

#ifdef ACS_DEBUG
        	acs_printk("%s: step 9\n", __func__);
#endif
	        /* More messed up locking ... */
	        spin_unlock_irq(&ide_lock);
	        device_unregister(&(hwif->gendev));
	        down(&hwif->gendev_rel_sem);
#ifdef ACS_DEBUG
        	acs_printk("%s: major=%d name=%s\n", __func__, hwif->major, hwif->name);
#endif

	        /*
	         * Remove us from the kernel's knowledge
	         */
        	blk_unregister_region(MKDEV(hwif->major, 0), MAX_DRIVES<<PARTN_BITS);
        	kfree(hwif->sg_table);
	        unregister_blkdev(hwif->major, hwif->name);
	}

        hotswap_stat[disknum] = DISK_REMOVED_OK;
        drive_stat[disknum] &= ~IDE_REMOVING;
//abort:
	up(&ide_cfg_sem);
#if	0
        if (ret & lcd_flag) {
                if (!(hotswap_stat[disknum] & DISK_ADDED_FAIL)){
                        hotswap_stat[disknum] &= ~DISK_REMOVING;
                        hotswap_stat[disknum] |= DISK_REMOVED_FAIL;
                }
        }
#endif

#ifdef ACS_DEBUG
        acs_printk("%s: OK\n", __func__);
#endif
        return ret;
}

int ide_add_disk_prep(unsigned char disknum)
{
	unsigned long hotswap_flag = 0;

	//if(hotswap_stat[disknum] & DISK_ADDING)
	//	return -1;
#ifdef	ACS_DEBUG
        acs_printk("%s: Disk %d was added!\n", __func__, disknum + 1);
#endif
        //write_kernellog("%d Disk %d was added!", WARN_LOG, disknum + 1);
        //lcd_string(LCD_L2, "Disk%d added!", disknum + 1);
        hotswap_stat[disknum] = DISK_ADDING;
        drive_stat[disknum] &= ~(IDE_FAIL | IDE_OFFLINE);
	drive_stat[disknum] |= (IDE_EXIST | IDE_ADDING);

	//Remove hotswap
        hotswap_flag |= 0x02 << (disknum * 4);  /* disk added */
	add_hotswap_queue(hotswap_flag);
        wake_up(&hotswap_wqueue);

	return 0;
}

int ide_remove_disk_prep(unsigned char disknum)
{
        ide_hwif_t *hwif;
        ide_drive_t *drive;
        ide_hwgroup_t *hwgroup;
	unsigned long flags, hotswap_flag = 0;
        unsigned char prm_scd, mst_slv;
	int i = 0;

	if(hotswap_stat[disknum] & (DISK_REMOVING | DISK_REMOVED_OK))
		return -1;
#ifdef ACS_DEBUG
        acs_printk("%s: disknum=%d, pid=%d\n", __func__, disknum, current->pid);
#endif

        get_disk_intf(disknum, &prm_scd, &mst_slv);
	hwif = &ide_hwifs[prm_scd];
	if (!hwif->present) {
#ifdef ACS_DEBUG
        	acs_printk("%s: !hwif->present\n", __func__);
#endif
                return 0;
        }
	drive = &hwif->drives[mst_slv];
	if (!drive->present) {
#ifdef ACS_DEBUG
        	acs_printk("%s: !drive->present\n", __func__);
#endif
                return 0;
        }

        //write_kernellog("%d Disk %d was removed!", WARN_LOG, disk_num(i));
        //lcd_string(LCD_L2, "Disk%d removed!", disk_num(i));
        hotswap_stat[disknum] = DISK_REMOVING;
        drive_stat[disknum] &= ~(IDE_EXIST | IDE_FAIL);
        drive_stat[disknum] |= (IDE_REMOVING | IDE_OFFLINE);

	spin_lock_irqsave(&ide_lock, flags);
	hwgroup = hwif->hwgroup;
#ifdef ACS_DEBUG
        	acs_printk("%s: hwgroup->drive:%s current drive:%s\n", __func__, hwgroup->drive->name, drive->name);
#endif
	if ((unsigned long)hwgroup->drive == (unsigned long)drive) {
#ifdef ACS_DEBUG
		acs_printk("%s: hwgroup->drive = current drive\n", __func__);
#endif
		hwgroup->busy = 1;
        	hwgroup->handler = NULL;
        	del_timer(&hwgroup->timer);

		spin_unlock(&ide_lock);
		local_irq_enable();
		if (drive->waiting_for_dma)
        		hwif->ide_dma_end(drive);    /* purge DMA mappings */
        	//ide_end_request(drive, 0, 0);
		spin_lock_irq(&ide_lock);
       		hwgroup->busy = 0;
	}
		
	spin_unlock_irqrestore(&ide_lock, flags);

        //turn_on_disk_fail(i);

	spin_lock_irq(&ide_lock);
	elv_drain_elevator(drive->queue);
        while (!list_empty(&drive->queue->queue_head)) {
               	HWGROUP(drive)->rq = list_entry_rq(drive->queue->queue_head.next);
		if (!(HWGROUP(drive)->rq->flags & REQ_STARTED)) {
			HWGROUP(drive)->rq->flags |= REQ_STARTED;
		}
		spin_unlock_irq(&ide_lock);
        	ide_end_request(drive, 0, 0);
		spin_lock_irq(&ide_lock);
		elv_drain_elevator(drive->queue);
               	i++;
	}
	spin_unlock_irq(&ide_lock);
#ifdef ACS_DEBUG
        acs_printk("%s: stop %d requests\n", __func__, i);
#endif
	//Remove hotswap
        hotswap_flag |= 0x01 << (disknum * 4);
 	add_hotswap_queue(hotswap_flag);
        wake_up(&hotswap_wqueue);
	
	return 0;
}

extern int md_add_disk(unsigned char);
extern int md_remove_disk(unsigned char);

void do_hotswap(unsigned int hotswap_flag)
{
        unsigned char op, i;

#ifdef	ACS_DEBUG
	acs_printk("%s: hotswap_flag=%x\n", __func__, hotswap_flag);
#endif
        for (i = 0; i < DISK_NUM; i++) {
                op = (hotswap_flag >> (i * 4)) & 0x03;

                if (op == 0x01) {       /* remove disk */
#if	0
                        if (ide_remove_rq(i) != 0){
#ifdef	ACS_DEBUG
				acs_printk("%s: ERROR: ide remove rq fail\n", __func__);
#endif
                                return;
                        }
#endif
                        if (md_remove_disk(i) != 0){
#ifdef	ACS_DEBUG
				acs_printk("%s: ERROR: md remove disk fail\n", __func__);
#endif
                                return;
                        }
                        if (ide_remove_disk(i, 1) != 0){
#ifdef	ACS_DEBUG
				acs_printk("%s: ERROR: ide remove disk fail\n", __func__);
#endif
                                return;
                        }
                } else if (op == 0x02) {       /* add disk */
			msleep(100);
                        if (ide_add_disk(i) != 0){
				printk("%s: ERROR: ide add disk fail\n", __func__);
                                return;
                        }
                        if (md_add_disk(i) != 0){
#ifdef	ACS_DEBUG
				acs_printk("%s: ERROR: md add disk fail\n", __func__);
#endif
                                return;
                        }
                }
        }
        return;
}


EXPORT_SYMBOL(do_hotswap);
EXPORT_SYMBOL(ide_add_disk_prep);
EXPORT_SYMBOL(ide_remove_disk_prep);
EXPORT_SYMBOL(get_disk_intf);
