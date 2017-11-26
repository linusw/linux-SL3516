/*
 * FILE NAME sl2312_obc.c
 *
 * BRIEF MODULE DESCRIPTION
 *  Driver for One_button_copy driver.
 *
 *  Author: Accusys NJ, Corp.
 *          bob cui & allen wu
 *
 * Copyright 2006 Accusys NJ, Corp.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMit8712D  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE	LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMit8712D   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, writ8712  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <asm/arch-sl2312/irqs.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/apm_bios.h>
#include <asm/arch/sl2312_gpio.h>

#include <asm/arch-sl2312/hardware.h>	//Neagus Added
#include <linux/acs_nas.h>
#include <linux/acs_char.h>


#define OBC_MINOR		248
#define OBC_INT_PIN		16	//USB copy

#ifndef SL2312_GPIO_BASE
#define SL2312_GPIO_BASE		0x4D000000
#endif
#define SL2312_GPIO_BASE_ADDR	IO_ADDRESS(SL2312_GPIO_BASE)

enum GPIO_REG
{
    GPIO_DATA_OUT   		= 0x00,
    GPIO_DATA_IN    		= 0x04,
    GPIO_PIN_DIR    		= 0x08,
    GPIO_BY_PASS    		= 0x0C,
    GPIO_DATA_SET   		= 0x10,
    GPIO_DATA_CLEAR 		= 0x14,
    GPIO_PULL_ENABLE 		= 0x18,
    GPIO_PULL_TYPE 			= 0x1C,
    GPIO_INT_ENABLE 		= 0x20,
    GPIO_INT_RAW_STATUS 	= 0x24,
    GPIO_INT_MASK_STATUS 	= 0x28,
    GPIO_INT_MASK 			= 0x2C,
    GPIO_INT_CLEAR 			= 0x30,
    GPIO_INT_TRIG 			= 0x34,
    GPIO_INT_BOTH 			= 0x38,
    GPIO_INT_POLAR 			= 0x3C
};


#define GPIO(x)						1<<x
#define GPIO_OBC_INT                     GPIO(OBC_INT_PIN)

int obc_signal; 

static int n99_obc_open(struct inode *inode, struct file *file)
{
        return 0;
}

static int n99_obc_release(struct inode *inode, struct file *file)
{
        return 0;
}

static int n99_obc_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
	switch(cmd){
		case OBC_GET_SIGNAL:
//			sl2312_led_ioctl(4, 1);	//Turn on blue led (USB activity)
			return obc_signal;
		case OBC_RESET_SIGNAL:
			//printk("OBC_RESET_SIGNAL\n");
//			sl2312_led_ioctl(4, 0);	//Turn off blue led (USB activity)
			obc_signal = 0;
			break;
		
		default:
			printk("n99_obc_ioctl:Invalid IOCTL:%d\n",cmd);
			return -EOPNOTSUPP;
	}	

	return 0;
}

static void n99_obc_interrupt(unsigned int src)
{
	obc_signal = 1;
	force_buzzer_on();
	mdelay(100);
	force_buzzer_off(); 
}


static struct file_operations n99_obc_fops = {
        .owner   = THIS_MODULE,
        .ioctl   = n99_obc_ioctl,
        .open    = n99_obc_open,
        .release = n99_obc_release,
};

/* include/linux/miscdevice.h */
static struct miscdevice n99_obc_miscdev =
{

        OBC_MINOR,
        "n99_obc",
        &n99_obc_fops
};

void n99_obc_direction()
{
	unsigned int data =0,addr;

        addr = SL2312_GPIO_BASE_ADDR + GPIO_PIN_DIR ;                   // set pin direction
        data = readl(addr);
        data &= GPIO_OBC_INT;
        writel(data,addr);
}

int __init n99_obc_init_module(void)
{
	int ret;

        misc_register(&n99_obc_miscdev);
        //printk("N299 one button copy init start.\n");
	
//	n99_obc_direction();

	ret = request_gpio_irq(OBC_INT_PIN, n99_obc_interrupt, 1, 1, 0);

 	if (ret){
		printk("Error: Register IRQ for One Button Copy:%d\n",ret);
		return(ret);
	}
	
	printk(KERN_EMERG "N299 one button copy init OK.\n");
        return(0);
}

void __exit n99_obc_cleanup_module(void)
{
	int ret;
	ret = free_gpio_irq(OBC_INT_PIN);
	if (ret){
		printk("Error: Unregister IRQ for One Button Copy:%d\n",ret);
	}
	
	printk(KERN_EMERG "Unregister IRQ for One Button Copy OK.\n");
	misc_deregister(&n99_obc_miscdev);
}

module_init(n99_obc_init_module);
module_exit(n99_obc_cleanup_module);

MODULE_AUTHOR("Bob Cui <cuixubo@accusys.com.cn> and Allen Wu .HAHA...");
MODULE_DESCRIPTION("N299 one button copy driver");
MODULE_LICENSE("GPL");
