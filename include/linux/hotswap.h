#ifndef	_HOTSWAP_H
#define _HOTSWAP_H

#include <linux/ide.h>

/* for disk status */
#define IDE_EXIST               0x0001
#define IDE_FAIL                0x0002
#define IDE_REMOVING            0x0004
#define IDE_ADDING              0x0008
#define IDE_OFFLINE             0x0010
#define IDE_WARNING             0x0020

/* for hotswap status */
#define DISK_ADDING             0x0001
#define DISK_ADDED_OK           0x0002
#define DISK_ADDED_FAIL         0x0004
#define DISK_REMOVING           0x0010
#define DISK_REMOVED_OK         0x0020
#define DISK_REMOVED_FAIL       0x0040

/* for hotswap ioctl */
#define HS_GET_HOTSWAP          0x0001
#define GET_NAS_MODEL           0x0002
#define GET_DISK_STAT           0x0003
#define GET_IO_SIZE_COUNT       0x0007
#define GET_IDE_STATUS        	0x0008
#define GET_HOTSWAP_STAT        0x0009
#define CLEAR_HOTSWAP_STAT      0x000A
/* for test ioctl */
#define ADD_DISK_TEST           0x0004
#define REMOVE_DISK_TEST        0x0005
#define HRESET_DISK_TEST	0x000B
#define SRESET_DISK_TEST	0x000C

#define HOTSWAP_MINOR		242
#define HOTSWAP_NUM     	(DISK_NUM * 4)
#define HDD0_PRESENT_SENSOR	7
#define HDD1_PRESENT_SENSOR	17
#define HDD2_PRESENT_SENSOR	6
#define HDD3_PRESENT_SENSOR	5	
#define HDD0_STATUS_LED		8
#define HDD1_STATUS_LED		15
#define HDD2_STATUS_LED		20
#define HDD3_STATUS_LED		31
#define HDD0_ACCESS_LED		11
#define HDD1_ACCESS_LED		12
#define HDD2_ACCESS_LED		13
#define HDD3_ACCESS_LED		14

extern unsigned long drive_stat[];
extern unsigned long hotswap_stat[];
extern wait_queue_head_t hotswap_wqueue;
//test
extern unsigned long ide_reg[2][2];

extern void add_hotswap_queue(unsigned long);
extern void do_hotswap(unsigned int);
extern int ide_add_disk_prep(unsigned char);
extern int ide_remove_disk_prep(unsigned char);
extern void set_disk_fail(unsigned char);
extern void get_disk_intf(unsigned char, unsigned char*, unsigned char*);
static inline unsigned char get_disk_num(char *name)
{
        return (unsigned char)(name[2] - 'a');
}

static inline unsigned char get_disknum_by_dev(dev_t dev)
{
	unsigned char disknum = 0;
	unsigned int major = MAJOR(dev);
	unsigned int minor = MINOR(dev);

	if (major == 3)
		disknum = minor / 64;
	else if (major == 22)
		disknum = 2 + minor / 64;
	else
		BUG_ON(1);

	//acs_printk("%s: dev=0x%x, disknum=%d\n", __func__, dev, disknum);
	return disknum;
}

static inline dev_t get_dev_by_disknum(unsigned char disknum, 
	unsigned char part)
{
	dev_t dev_num = 0;
	
	switch (disknum) {
	case 0:
		dev_num = MKDEV(3, 0 + part);
		break;
	case 1:
		dev_num = MKDEV(3, 64 + part);
		break;
	case 2:
		dev_num = MKDEV(22, 0 + part);
		break;
	case 3:
		dev_num = MKDEV(22, 64 + part);
		break;
	default:
		BUG_ON(1);
	}
	
	return dev_num;
}

static inline dev_t get_dev_by_name(char *name)
{
        unsigned char disknum;
        dev_t dev_num = 0;

        disknum = get_disk_num(name);
        if(disknum >= DISK_NUM)
                BUG_ON(1);

        switch  (disknum) {
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

static inline int disk_fail(unsigned char disknum)
{

	if (!(drive_stat[disknum] & IDE_EXIST) || (drive_stat[disknum] & IDE_FAIL)) {
                return 1;
        } else {
                return 0;
	}
}

extern unsigned char disknum_to_pin(unsigned char, unsigned char);
extern void hdd_status_led_ctl(unsigned char, unsigned char);
#define	DEFAULT_SECTOR_SIZE	512

#endif	/* HOTSWAP_H */
