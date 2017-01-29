/**********************************************************
* FILE NAME gemini_flash_chip.c
*
* Description	: 
*		Get flash chip.
*
* Author: ALPHA, Corp.
* 	Micle 20090325 for EN29LV400A
* 
*
**********************************************************/

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/sl2312.h>
#include <asm/arch/irqs.h>
#include <asm/arch/gemini_gpio.h>
#include <asm/delay.h>

#define GET_FLASH_CHIP  	0
extern unsigned int FIND_EN29LV400A;

static int flash_chip_probe_open(struct inode *inode, struct file *file)
{ 
	return 0;
}


static int flash_chip_probe_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int flash_chip_probe_ioctl(struct inode *inode, struct file *file,unsigned int cmd, unsigned int arg)
{
	unsigned int data=0;

	get_user(data,(unsigned int *)arg);
	switch(cmd) {
		case GET_FLASH_CHIP: 
			data = FIND_EN29LV400A;
			put_user(data,(unsigned int *)arg);
			break;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static struct file_operations flash_chip_probe_fops = {
	.owner	=	THIS_MODULE,
	.ioctl	=	flash_chip_probe_ioctl,
	.open	=	flash_chip_probe_open,
        .release =	flash_chip_probe_release,
};

/* GPIO_MINOR in include/linux/miscdevice.h */
static struct miscdevice flash_chip_probe_miscdev =
{
	FLASH_CHIP_MINOR,
	"sl_flash",
	&flash_chip_probe_fops
};

int __init flash_chip_probe_init(void)
{
	misc_register(&flash_chip_probe_miscdev);
	printk("flash probe init\n");
	
	return 0;
}	

void __exit flash_chip_probe_exit(void)
{
	misc_deregister(&flash_chip_probe_miscdev);
}

module_init(flash_chip_probe_init);
module_exit(flash_chip_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("micle@cn.alphanetworks.com");
MODULE_DESCRIPTION("Alpha flash chip probe");

