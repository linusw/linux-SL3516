#include <linux/module.h>
#include <linux/ide.h>
#include <linux/miscdevice.h>
#include <linux/acs_nas.h>
#include <linux/hotswap.h>
#include <asm/arch/gemini_gpio.h>

unsigned long drive_stat[DISK_NUM];
unsigned long hotswap_stat[DISK_NUM];

//test
unsigned long ide_reg[2][2];

static unsigned long hotswap_q[HOTSWAP_NUM];
static unsigned int hotswap_num, hs_head = 0, hs_tail = 0;
wait_queue_head_t hotswap_wqueue;

static int hotswap_open(struct inode *inodep, struct file *filep)
{
	return 0;
}
static int hotswap_release(struct inode *inodep, struct file *filep)
{
	return 0;
}

static void accu_clear_hotswap_stat(void)
{
	int index;
	for (index = 0; index < MAX_HWIFS; ++index)
		hotswap_stat[index] = 0;
}

extern void ide_hard_reset(unsigned char);
extern void get_ide_stat(unsigned char*);

static int hotswap_ioctl(struct inode *inodep, struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int disk_num = 0;
	unsigned long model;
	unsigned char ide_stat[4];
	unsigned char mst_slv, fst_scd;

	if (!capable(CAP_SYS_ADMIN)) 
		return -EACCES;

	switch (cmd) {
	case HS_GET_HOTSWAP:
		return 0;
	case GET_NAS_MODEL:
		model = NAS_MODEL;
		ret = copy_to_user((char *)arg, &model, sizeof(model));
		return ret;
	case GET_DISK_STAT:
		//for n299 
		ret = copy_to_user((char *)arg, drive_stat+2, (DISK_NUM-2)*sizeof(int));
		return ret;
	case GET_HOTSWAP_STAT:
		ret = copy_to_user((char *)arg, hotswap_stat+2, (DISK_NUM-2)*sizeof(int));
		return ret;
	case CLEAR_HOTSWAP_STAT:
		accu_clear_hotswap_stat();
		return 0;
	case ADD_DISK_TEST:
		ret = copy_from_user(&disk_num, (int *)arg, sizeof(int));
		ide_add_disk_prep(disk_num - 1);
		return ret;
	case REMOVE_DISK_TEST:
		ret = copy_from_user(&disk_num, (int *)arg, sizeof(int));
		ide_remove_disk_prep(disk_num - 1);
		return ret;
	case HRESET_DISK_TEST:
		ret = copy_from_user(&disk_num, (int *)arg, sizeof(int));
		ide_hard_reset(disk_num - 1);
		return 0;
	case SRESET_DISK_TEST:
		ret = copy_from_user(&disk_num, (int *)arg, sizeof(int));
		ide_soft_reset(disk_num - 1);
		return 0;
	case GET_IDE_STATUS:
		get_ide_stat(ide_stat);
		//ide_stat[0] = (unsigned char)readb(ide_reg[0][0] + 7);
		//ide_stat[1] = (unsigned char)readb(ide_reg[1][0] + 7);
		ret = copy_to_user((unsigned char *)arg, ide_stat, 4);
		return 0;
	default:
		return(-EPERM);
	}
}

struct file_operations hotswap_fops =
{
        owner:          THIS_MODULE,
        llseek:         NULL,
        read:           NULL,
        write:          NULL,
        ioctl:          hotswap_ioctl,
        open:           hotswap_open,
        release:        hotswap_release,
};

struct miscdevice hotswap =
{
        HOTSWAP_MINOR,
        "hotswap",
        &hotswap_fops
};

/**
 *    HDD Present sensor GPIO pin to disk number
 */
static unsigned char hps_to_disknum(unsigned char pin)
{
	unsigned char ret = 0;

	switch (pin) {
	case HDD0_PRESENT_SENSOR:
		ret = 0;
		break;
	case HDD1_PRESENT_SENSOR:
		ret = 1;
		break;
	case HDD2_PRESENT_SENSOR:
		ret = 2;
		break;
	case HDD3_PRESENT_SENSOR:
		ret = 3;
		break;
	default:
		BUG_ON(1);
	}

	return ret;
}

/**   
 *   Get the GPIO pin for HDD
 *   @index: 
 * 	0: get HDD Present sensor GPIO pin 
 *	1: get HDD Status LED GPIO pin
 * 	2: get HDD Access LED GPIO pin
 */
unsigned char 
disknum_to_pin(unsigned char disknum, unsigned char index)
{
	unsigned char ret = 0;

	if (index > 2) {
		BUG_ON(1);
		return 0xff;
	}

	switch (disknum) {
	case 0:
		if (index == 0) 
			ret = HDD0_PRESENT_SENSOR; 
		else if (index == 1) 
			ret = HDD0_STATUS_LED;		
		else 
			ret = HDD0_ACCESS_LED;
		break;
	case 1:
		if (index == 0) 
			ret = HDD1_PRESENT_SENSOR; 
		else if (index == 1) 
			ret = HDD1_STATUS_LED;		
		else 
			ret = HDD1_ACCESS_LED;
		break;
	case 2:
		if (index == 0) 
			ret = HDD2_PRESENT_SENSOR; 
		else if (index == 1) 
			ret = HDD2_STATUS_LED;		
		else 
			ret = HDD2_ACCESS_LED;
		break;
	case 3:
		if (index == 0) 
			ret = HDD3_PRESENT_SENSOR; 
		else if (index == 1) 
			ret = HDD3_STATUS_LED;		
		else 
			ret = HDD3_ACCESS_LED;
		break;
	default:
		BUG_ON(1);
	}

	return ret;
}

void hdd_status_led_ctl(unsigned char disknum, unsigned char on)
{
	return;
	unsigned char status_led_pin = disknum_to_pin(disknum, 1);
	
	if (on != 0 && on !=1) {
		BUG_ON(1);
		return;
	}

	set_gemini_gpio_io_mode(status_led_pin, 1);
        set_gemini_gpio_pin_status(status_led_pin, on);
}

static void hotswap_intr(int pin)
{
	unsigned char disknum = 0;
	unsigned long flags = 0;

	return;
	local_irq_save(flags);
	disknum = hps_to_disknum(pin);

#ifdef	ACS_DEBUG
	acs_printk("%s: hotswap interrupt\n", __func__);
#endif
	//goto hotswap_intr_out;

	gemini_gpio_pin_write((unsigned char)pin, 0, 0x08); //set dir to input
	if (gemini_gpio_pin_read((unsigned char)pin, 0x04)) {
		ide_remove_disk_prep(disknum);
	} else {
		ide_add_disk_prep(disknum);
	}

//hotswap_intr_out:
	local_irq_restore(flags);
	return;
}

static unsigned long get_hotswap(void);

int hotswapd(void* unused)
{
        unsigned long   flag;

        while (1) {
                flag = get_hotswap();
                do_hotswap(flag);
        }

	printk("ERROR: hotswapd stopped!!!\n");
        return(0);
}

static int __init hotswap_init(void)
{
	int i, ret = -1;

	//allen modify 20070316
//	init_waitqueue_head(&hotswap_wqueue);
	memset(hotswap_stat, 0, sizeof(unsigned long) * DISK_NUM);
	memset(drive_stat, 0, sizeof(unsigned long) * DISK_NUM);

/*
	for (i = 0; i < DISK_NUM; i++) {
        	if (request_gpio_irq(disknum_to_pin(i, 0), &hotswap_intr, 0, 0, 1)) {
                	BUG();
                	panic("ERROR: request_gpio_irq() fails!\n");
        	}
	}
*/

//test
//        kernel_thread(hotswapd, NULL, CLONE_FS | CLONE_SIGHAND);

	ret = misc_register(&hotswap);
	if(ret){
		printk("ERROR: hotswap_init.\n");
		return ret;
	}

	return ret;
}

static void __exit hotswap_exit(void)
{
	misc_deregister(&hotswap);
	return;
}

static unsigned long get_hotswap(void)
{
	unsigned long flags, hotswap_flag = 0;

	if (hs_head == hs_tail) {
		sleep_on(&hotswap_wqueue);
	}

	local_irq_save(flags);
#ifdef	ACS_DEBUG
	acs_printk("hotswap(): head = %d, tail = %d\n", hs_head, hs_tail);
#endif
	hotswap_flag = hotswap_q[hs_tail];
	hs_tail = (hs_tail+1) % HOTSWAP_NUM;
	hotswap_num--;
	local_irq_restore(flags);

	return(hotswap_flag);
}

void add_hotswap_queue(unsigned long hotswap_flag)
{
        if (hotswap_num == HOTSWAP_NUM - 1) {
                printk("add_hotswap_queue(): hotswap queue is full!\n");
                return;
        }
        hotswap_q[hs_head] = hotswap_flag;
        hs_head = (hs_head+1) % HOTSWAP_NUM;
        hotswap_num++;
}

EXPORT_SYMBOL(disknum_to_pin);
EXPORT_SYMBOL(hdd_status_led_ctl);
EXPORT_SYMBOL(drive_stat);
EXPORT_SYMBOL(hotswap_stat);
EXPORT_SYMBOL(add_hotswap_queue);
//test
EXPORT_SYMBOL(ide_reg);

module_init(hotswap_init);
module_exit(hotswap_exit);
