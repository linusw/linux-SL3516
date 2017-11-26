/*
 * FILE NAME sl2312_led.c
 *
 *  Author: Accusys NJ, Corp.
 *          Neagus 
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
#include <linux/ide.h>
#include <linux/hotswap.h>

#define SATA1_ACT	0
#define SATA1_FAIL	1
#define SATA2_ACT	2
#define SATA2_FAIL	3
#define	USB_ACT		4
#define SYSTEM_BUSY	5
#define SYSTEM_FAIL	6
#define USB_FAIL	7
#define USB_COPY	16
#define	GMAC_0		21
#define	GMAC_1		22
#define GMAC_0_ORANGE	23
#define GMAC_1_ORANGE	24
#define QC		30
#define RESET		31
#define HDD_FORCE_ON	32
#define FORCESYSTEM	99

#define GMAC0_PHY_ADDR	1
#define GMAC1_PHY_ADDR	2

#define HDD1_ACT_LED            0
#define HDD1_FAIL_LED           1
#define HDD2_ACT_LED            2
#define HDD2_FAIL_LED           3

#define TURNONALL	33
#define TURNOFFALL	34
#define LED_MINOR	246

unsigned char led_signal = 9;
unsigned char led_signal_0 = 9;
unsigned char led_signal_1 = 9;
unsigned char led_signal_2 = 9;
unsigned char led_signal_3 = 9;
unsigned char led_signal_4 = 9;
unsigned char led_signal_5 = 9;
unsigned char led_signal_6 = 9;
unsigned char led_signal_7 = 9;

extern unsigned long drive_stat[DISK_NUM];
extern unsigned char gemini_gpio_pin_read(unsigned char, unsigned char);
extern void mii_write(unsigned char, unsigned char, unsigned int);

unsigned char dn_to_pin(unsigned char disknum, unsigned char index)
{
	unsigned char ret = 0;
        switch (disknum) {
        case 0:
                if (index == 1)
                        ret = HDD1_FAIL_LED;
                else
                        ret = HDD1_ACT_LED;
                break;
        case 1:
                if (index == 1)
                        ret = HDD2_FAIL_LED;
                else
                        ret = HDD2_ACT_LED;
                break;
        }
	return ret;
}

static inline unsigned char led_get_disk_num(char *name)
{
	unsigned char ret;
	ret = name[2] - 'a';
	ret -=2;
	return ret;
       // return (unsigned char)(name[2] - 'a');
}


void hdd_status_led(unsigned char disknum, int index, int act)
{
        unsigned char status_led_pin = dn_to_pin(disknum, index);

        if (!act) {
                gemini_gpio_pin_write(status_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(status_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(status_led_pin, 1, 0x10); //turn off LED
        } else {
                gemini_gpio_pin_write(status_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(status_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(status_led_pin, 0, 0x10); //turn on LED
        }
}

void hdd_access_led(void *data, int act)
{
	ide_drive_t *drive = (ide_drive_t *)data;
	unsigned char access_led_pin;
	if (act == 3) {
        	access_led_pin = dn_to_pin(led_get_disk_num(drive->name), 1);
		drive_stat[led_get_disk_num(drive->name)+2] |= IDE_OFFLINE;
		drive_stat[led_get_disk_num(drive->name)+2] |= IDE_FAIL;
	} else {
        	access_led_pin = dn_to_pin(led_get_disk_num(drive->name), 2);
	}

        if (!act) {
                gemini_gpio_pin_write(access_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(access_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(access_led_pin, 1, 0x10); //turn off LED
        } else {
                gemini_gpio_pin_write(access_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(access_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(access_led_pin, 0, 0x10); //turn on LED
        }
}


void force_led(unsigned char pin, int act)
{
        if (!act) {
                gemini_gpio_pin_write(pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(pin, 1, 0x14); //clear
                gemini_gpio_pin_write(pin, 1, 0x10); //turn off LED
        } else {
                gemini_gpio_pin_write(pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(pin, 1, 0x14); //clear
                gemini_gpio_pin_write(pin, 0, 0x10); //turn on LED
        }
}

static void blink_gmac_led_act(unsigned char pin, int act)
{
	unsigned char	phy_addr;
	unsigned int	value;
	
	if(pin == 21){
		phy_addr = GMAC0_PHY_ADDR;
		value = 0x999; //force on
	}else if(pin == 22){
		phy_addr = GMAC1_PHY_ADDR;
		value = 0x999; //force on
	}else if(pin == 23){
		phy_addr = GMAC0_PHY_ADDR;
		value = 0x477; //orange(1000Mbps) blink on
	}else if(pin == 24){
		phy_addr = GMAC1_PHY_ADDR;
		value = 0x477; //orange(1000Mbps) blink on
	}

	if (!act) {
		//set led timer control
		mii_write(phy_addr, 0x16, 0x3); //page address
    		mii_write(phy_addr, 0x12, 0x0000); //default
		//set led polarity
       		mii_write(phy_addr, 0x16, 0x3); //page address
       		mii_write(phy_addr, 0x11, 0x0000); //low active
		//set led function
		mii_write(phy_addr, 0x16, 0x3); //page address
                mii_write(phy_addr, 0x10, 0x888);  //force off

	}else{
	        //set led timer control
                mii_write(phy_addr, 0x16, 0x3); //page address
                mii_write(phy_addr, 0x12, 0x0000); //default
                //set led polarity
                mii_write(phy_addr, 0x16, 0x3); //page address
                mii_write(phy_addr, 0x11, 0x0000); //low active
                //set led function
                mii_write(phy_addr, 0x16, 0x3); //page address
		mii_write(phy_addr, 0x10, value);  //on
	
	}	
}

static void blink_led(unsigned char pin)
{
        unsigned char val, value = 1;

	switch(pin){
	case 0:
        	while(1){
	       		if (led_signal_0 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_0 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}

	case 1:
        	while(1){
	       		if (led_signal_1 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_1 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}
	
	case 2:
        	while(1){
	       		if (led_signal_2 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_2 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}
	
	case 3:
        	while(1){
	       		if (led_signal_3 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_3 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}

	case 4:
        	while(1){
	       		if (led_signal_4 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_4 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}

	case 5:
        	while(1){
	       		if (led_signal_5 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_5 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}
	
	case 6:
        	while(1){
	       		if (led_signal_6 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_6 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}

	case 7:
        	while(1){
	       		if (led_signal_7 == 0){
     				value = gemini_gpio_pin_read(pin, 0x04);
				if(value == 1)
					val = 0;
				else if(value == 0)
					val = 1;
	
        	       		 gemini_gpio_pin_write(pin, 1, 0x08);
	             		 gemini_gpio_pin_write(pin, 1, 0x14);
               			 gemini_gpio_pin_write(pin, val, 0x10);

           		}else if(led_signal_7 == 1){
                        	break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
               		schedule_timeout(HZ/10);
        	}
	}
	return;

}

void system_led_act(unsigned char pin, int act)
{
//0: turn on; 1: turn off; By Neagus

	if (!act) {
		if(pin == 4)
			led_signal_4 = 1;
		else if(pin == 5)
			led_signal_5 = 1;
		else if(pin == 6)
			led_signal_6 = 1;
		else if(pin == 7)
			led_signal_7 = 1;

                gemini_gpio_pin_write(pin, 1, 0x08);    //set dir to output
                gemini_gpio_pin_write(pin, 1, 0x14);    //clear
                gemini_gpio_pin_write(pin, 1, 0x10);    //turn off LED
        } else {
		if(pin == 4)
			led_signal_4 = 0;
		else if(pin == 5)
			led_signal_5 = 0;
		else if(pin == 6)
			led_signal_6 = 0;
		else if(pin == 7)
			led_signal_7 = 0;

              	//blink_led(pin);
                gemini_gpio_pin_write(pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(pin, 1, 0x14); //clear
                gemini_gpio_pin_write(pin, 0, 0x10); //turn on LED
        }
}

void hdd_led_ctl(unsigned char hdd_led_pin, int act)
{

        if (!act) {
		if(hdd_led_pin == 0)
			led_signal_0 = 1;
		else if(hdd_led_pin == 1)
			led_signal_1 = 1;
		else if(hdd_led_pin == 2)
			led_signal_2 = 1;
		else if(hdd_led_pin == 3)
			led_signal_3 = 1;

                gemini_gpio_pin_write(hdd_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(hdd_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(hdd_led_pin, 1, 0x10); //turn off LED
        } else {
		if(hdd_led_pin == 0)
			led_signal_0 = 0;
		else if(hdd_led_pin == 1)
			led_signal_1 = 0;
		else if(hdd_led_pin == 2)
			led_signal_2 = 0;
		else if(hdd_led_pin == 3)
			led_signal_3 = 0;

              	//blink_led(hdd_led_pin);
                gemini_gpio_pin_write(hdd_led_pin, 1, 0x08); //set dir to output
                gemini_gpio_pin_write(hdd_led_pin, 1, 0x14); //clear
                gemini_gpio_pin_write(hdd_led_pin, 0, 0x10); //turn on LED
        }
}

int sl2312_led_ioctl(unsigned char cmd, int act)
{
	if(cmd <= SATA2_FAIL){
		hdd_led_ctl(cmd, act);

	}else{
		system_led_act(cmd, act);
	
	}

	return 0;
}

/*
 * Add jumper2, the GPIO1_30 is 0, do QC test. 
 * No jumper2, the GPIO1_30 is 1, don't do QC test.
 */
unsigned char sl2312_qc_stat(void)
{
	unsigned char value = 1;
	gemini_gpio_pin_write(62, 0, 0x08);
	value = gemini_gpio_pin_read(62, 0x04);
	return value;
}

static int n99_led_open(struct inode *inode, struct file *file)
{
        return 0;
}

static int n99_led_release(struct inode *inode, struct file *file)
{
        return 0;
}

static int n99_led_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
	int	act;
	unsigned char value = 1;
	int ret;

	switch(cmd) {
		case TURNONALL:
			act = 1;	
			force_led(0, act);
			force_led(2, act);
			force_led(4, act);
			force_led(5, act);
			blink_gmac_led_act(21, act);
			blink_gmac_led_act(22, act);
                        break;

		case TURNOFFALL:
			act = 0;	
			force_led(0, act);
			force_led(2, act);
			force_led(4, act);
			force_led(5, act);
			blink_gmac_led_act(21, act);
			blink_gmac_led_act(22, act);
                        break;

		case SATA1_ACT:
			hdd_led_ctl(0 , (int)arg);
                        break;

		case SATA1_FAIL:
			hdd_led_ctl(1 , (int)arg);
                        break;

		case SATA2_ACT:
			hdd_led_ctl(2 , (int)arg);
                        break;

		case SATA2_FAIL:
			hdd_led_ctl(3 , (int)arg);
                        break;

		case USB_ACT:
			system_led_act(4 , (int)arg);
                        break;

		case SYSTEM_BUSY:
			system_led_act(5 , (int)arg);
                        break;

		case SYSTEM_FAIL:
			system_led_act(6 , (int)arg);
                        break;

		case USB_FAIL:
			system_led_act(7 , (int)arg);
                        break;

		case USB_COPY:
                        break;

		case GMAC_0:	
			blink_gmac_led_act(21, (int)arg);
			break;

		case GMAC_1:	
			blink_gmac_led_act(22, (int)arg);
			break;

		case GMAC_0_ORANGE:	
			blink_gmac_led_act(23, (int)arg);
			break;

		case GMAC_1_ORANGE:	
			blink_gmac_led_act(24, (int)arg);
			break;

		case QC:
			value = sl2312_qc_stat();
			ret = put_user((int)value, (int *)arg);
			break;
			
		case RESET:		
                        break;

		case HDD_FORCE_ON:
			//hdd_status_led(0, 1, 1);
			//hdd_status_led(1, 1, 1);
			hdd_status_led(0, 1, (int)arg);
			hdd_status_led(1, 1, (int)arg);
                        break;

		case FORCESYSTEM:
			system_led_act(5 , (int)arg);
			break;

		Default:
			return -1;
	}
	
	return 0;

}

static struct file_operations n99_led_fops = {
        .owner   = THIS_MODULE,
        .ioctl   = n99_led_ioctl,
        .open    = n99_led_open,
        .release = n99_led_release,
};

/* include/linux/miscdevice.h */
static struct miscdevice n99_led_miscdev =
{

        LED_MINOR,
        "n99_led",
        &n99_led_fops
};

int __init led_init(void)
{

        misc_register(&n99_led_miscdev);

        return 0;
}

void __exit led_exit(void)
{
        misc_deregister(&n99_led_miscdev);
}

module_init(led_init);
module_exit(led_exit);

