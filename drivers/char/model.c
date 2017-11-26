/*
 * FILE NAME model.c
 *
 *  Author: Accusys NJ, Corp.
 *          Neagus 
 * N299: 2 disks
 * N199: 1 disks
 *
 * GPIO0_20 (J5):
 *  1 = N299
 *  0 = N199 
 *
 * Copyright 2007 Accusys NJ, Corp.
 *
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/arch/gemini_gpio.h>
#include <asm/arch/hardware.h>
#include <linux/acs_nas.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

#define MODEL_MINOR	250
#define GET_MODEL	0x1

extern unsigned char gemini_gpio_pin_read(unsigned char, unsigned char);

int get_model()
{
	int	ret;

	gemini_gpio_pin_write(20, 0, 0x08);
        ret=(int)gemini_gpio_pin_read(20, 0x04);
	return ret;
}

static int model_open(struct inode *inode, struct file *file)
{
        return 0;
}

static int model_release(struct inode *inode, struct file *file)
{
        return 0;
}

static int model_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, int arg)
{
	switch(cmd) {
		case GET_MODEL:
			gemini_gpio_pin_write(20, 0, 0x08);
			put_user((int)gemini_gpio_pin_read(20, 0x04), (int *)arg);
			break;

		Default:
			return -1;
	}
	
	return 0;

}

static struct file_operations model_fops = {
        .owner   = THIS_MODULE,
        .ioctl   = model_ioctl,
        .open    = model_open,
        .release = model_release,
};

/* include/linux/miscdevice.h */
static struct miscdevice model_miscdev =
{

        MODEL_MINOR,
        "model",
        &model_fops
};

int __init model_init(void)
{

        misc_register(&model_miscdev);
        return 0;
}

void __exit model_exit(void)
{
        misc_deregister(&model_miscdev);
}

module_init(model_init);
module_exit(model_exit);

