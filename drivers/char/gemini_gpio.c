/*
 * FILE NAME Gemini_gpio.c
 *
 * BRIEF MODULE DESCRIPTION
 *  API for gemini GPIO module
 *  Driver for gemini GPIO module
 *
 *  Author: StorLink, Corp.
 *          Jason Lee <jason@storlink.com.tw>
 *
 * Copyright 2005 StorLink, Corp.
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
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/acs_nas.h>

#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/sl2312.h>
#include <asm/arch/irqs.h>
#include <asm/arch/gemini_gpio.h>

#define GEMINI_GPIO_BASE1		IO_ADDRESS(SL2312_GPIO_BASE)
#define GEMINI_GPIO_BASE2		IO_ADDRESS(SL2312_GPIO_BASE1)

#define GPIO_SET	2
#define MAX_GPIO_LINE	32*GPIO_SET

wait_queue_head_t gemini_gpio_wait[MAX_GPIO_LINE];

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

unsigned int regist_gpio_int0=0,regist_gpio_int1=0;
static int gemini_gpio_debug=0;
#define DEB(x)  if (gemini_gpio_debug>=1) x


struct gpio_irqaction {
	void (*handler)(unsigned int);
};

struct gpio_irqaction irq_handle[MAX_GPIO_LINE];



// set: 1-2
// mode:0: input, 1: output
void set_gemini_gpio_io_mode(unsigned char pin, unsigned char mode)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status,addr;
	
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1)+GPIO_PIN_DIR;
	status = readl(addr);
	
	status &= ~(1 << (pin %32));
	status |= (mode << (pin % 32));
	writel(status,addr);
}

// set: 1-2
// high: 1:high, 0:low
void set_gemini_gpio_pin_status(unsigned char pin, unsigned char high)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status=0,addr;
	
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1)+GPIO_DATA_SET;

	status &= ~(1 << (pin %32));
	status |= (high << (pin % 32));
	writel(status,addr);
}

#ifdef ORION_430ST
unsigned char gemini_gpio_pin_read(unsigned char pin, unsigned char addr)
{
	unsigned char set = pin >> 5;            // each GPIO set has 32 pins
        unsigned long status, address;

        address = (set ? GEMINI_GPIO_BASE2 : GEMINI_GPIO_BASE1) + addr;
        status = readl(address);
//      return (status & (1 << pin)) ? 1 : 0;
        return (status & (1 << (pin % 32))) ? 1 : 0; //Sunny 20070315
}

void gemini_gpio_pin_write(unsigned char pin, unsigned char high, unsigned char addr)
{
        unsigned char set = pin >> 5;            // each GPIO set has 32 pins
        unsigned int status = 0, address;

        address = (set ? GEMINI_GPIO_BASE2 : GEMINI_GPIO_BASE1) + addr;
        status = readl(address);
        status &= ~(1 << (pin % 32));
        status |= (high << (pin % 32));
        writel(status, address);
}
#endif

// set: 1-2
// return: 1:high, 0:low
int get_gemini_gpio_pin_status(unsigned char pin)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status,addr;
	
	addr = set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1;
	status = readl(addr);
	return (status&(1<<(pin%32)))?1:0; 
}


// wait interrupt event
int wait_gemini_gpio_pin_interrupt(struct gemini_gpio_ioctl_data *ioctl_data)
{
		
	DEB(printk("wait GPIO_%d Interrupt\n",ioctl_data->pin));
	if(ioctl_data->use_default==1)
		init_gpio_int(ioctl_data->pin, 0, 0, 0);
	else
		init_gpio_int(ioctl_data->pin, ioctl_data->trig_type, ioctl_data->trig_polar, \
					ioctl_data->trig_both);
		
	interruptible_sleep_on(&gemini_gpio_wait[ioctl_data->pin]);
	return 0;
}

void init_gpio_int(__u32 pin,__u8 trig_type,__u8 trig_polar,__u8 trig_both)
{
	unsigned int data =0,addr,base;
	unsigned char set = pin >>5;	
	
	base = set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1;
	
	addr = base + GPIO_INT_TRIG ;
	data = readl(addr);
//	data &= ~(1<<(pin%32)) ;			// edge trig
	trig_type ? (data|=(1<<(pin%32))):(data &= ~(1<<(pin%32)));
	writel(data,addr);
	
	addr = base + GPIO_INT_POLAR ;
	data = readl(addr);
//	data &= ~(1<<(pin%32));				// rising edge
	trig_polar ? (data|=(1<<(pin%32))):(data &= ~(1<<(pin%32)));
	writel(data,addr);
	
	addr = base + GPIO_INT_BOTH ;
	data = readl(addr);
//	data &= ~(1<<(pin%32)) ;			// single edge
	trig_both ? (data|=(1<<(pin%32))):(data &= ~(1<<(pin%32)));
	writel(data,addr);
	
	addr = base + GPIO_INT_MASK ;
	data = readl(addr);
	data &= ~(1<<(pin%32));				// unmask
	writel(data,addr);
	
	addr = base + GPIO_INT_CLEAR ;
	data = readl(addr);
	data |= 1<<(pin%32) ;				// Clear interrupt before Enable it
	writel(data,addr);	
	
	addr = base + GPIO_INT_ENABLE ;
	data = readl(addr);
	data |= 1<<(pin%32) ;				// Enable interrupt
	writel(data,addr);	

}

static irqreturn_t gpio_int1(void)
{
	unsigned int i,int_src,addr,data,base,int_status;
	unsigned int mask_stat;

#ifdef	ACS_DEBUG
	acs_printk("%s: start\n", __func__);
#endif	
	base = GEMINI_GPIO_BASE1;
	addr = base + GPIO_INT_RAW_STATUS ;
	int_status = int_src = readl(addr);
	
	addr = base + GPIO_INT_MASK;
	mask_stat = readl(addr);
	
	int_src &= ~mask_stat;	// ignore INT that was masked
#ifdef	ACS_DEBUG
	acs_printk("%s: int_status=%x mask_stat=%x\n", __func__, int_status, mask_stat);
#endif	
	for(i=0;int_src;int_src>>=1,i++) {
		if((int_src&(1))==0)
			continue;
			
		addr = base + GPIO_INT_MASK;
		data = readl(addr);
		data |= (1<<i);	// mask INT
		writel(data,addr);
		if( (regist_gpio_int0&(1<<i))&&irq_handle[i].handler ){
			//irq_handle[i].handler(int_status);
			irq_handle[i].handler(i);
		}
		else{
			wake_up(&gemini_gpio_wait[i]);	// wake up 
			DEB(printk("wake up gpio %d",i));
#ifdef	ACS_DEBUG
			acs_printk("%s: wake up gpio %d\n", __func__, i);
			acs_printk("%s: regist_gpio_int0=%x %d\n", __func__, regist_gpio_int0, !irq_handle[i].handler);
#endif	
		}
		addr = base + GPIO_INT_CLEAR ;
		data = readl(addr);
		data |= (1<<i);	// clear INT
		writel(data,addr);
		
		addr = base + GPIO_INT_MASK ;
		data = readl(addr);
		data &= ~(1<<i);// unmask INT
		writel(data,addr);
		
	}

	return IRQ_HANDLED;
}

static irqreturn_t gpio_int2(void)
{
	unsigned int i,int_src,addr,data,base,int_status;
	unsigned int mask_stat;
	
	base = GEMINI_GPIO_BASE2;
	addr = base + GPIO_INT_RAW_STATUS ;
	int_status = int_src = readl(addr);
	
	addr = base + GPIO_INT_MASK;
	mask_stat = readl(addr);
	
	int_src &= ~mask_stat;							// ignore INT that was masked
	for(i=0;int_src;int_src>>=1,i++) {
		if((int_src&(1))==0)
			continue;
			
		addr = base + GPIO_INT_MASK ;
		data = readl(addr);
		data |= (1<<i);								// mask INT
		writel(data,addr);
		if( (regist_gpio_int1&(1<<i))&&irq_handle[i+32].handler ){
			//irq_handle[i+32].handler(int_status);
			irq_handle[i+32].handler(i+32);
		}
		else{
			wake_up(&gemini_gpio_wait[i+32]);				// wake up 
			DEB(printk("wake up gpio %d",i+32));
		}
		addr = base + GPIO_INT_CLEAR ;
		data = readl(addr);
		data |= (1<<i) ;								// clear INT
		writel(data,addr);
		
		addr = base + GPIO_INT_MASK ;
		data = readl(addr);
		data &= ~(1<<i);								// unmask INT
		writel(data,addr);
		
	}

	return IRQ_HANDLED;
}

static int gemini_gpio_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int gemini_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}


static int gemini_gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct gemini_gpio_ioctl_data ioctl_data;
	
	if (copy_from_user(&ioctl_data, (struct gemini_gpio_ioctl_data *)arg, sizeof(ioctl_data)))
		return -EFAULT;
	
	if (ioctl_data.pin >= MAX_GPIO_LINE) 	
	{
		printk("gemini_gpio ioctl fail[pin > %d]\n",MAX_GPIO_LINE);
		return -EFAULT;
	}
	DEB(printk("gemini_gpio ioctl successly\n"));

	switch(cmd) {
		case GEMINI_SET_GPIO_PIN_DIR:			// Set pin Direction
#ifdef	ACS_DEBUG
			acs_printk("%s: SET_PIN_DIR. pin:%d dir:%d\n", __func__, ioctl_data.pin, ioctl_data.status);
#endif
			set_gemini_gpio_io_mode(ioctl_data.pin, ioctl_data.status);
			break;

		case GEMINI_SET_GPIO_PIN_STATUS:		// Set pin Status
#ifdef	ACS_DEBUG
			acs_printk("%s: SET_PIN_STATUS. pin:%d bit:%d\n", __func__, ioctl_data.pin, ioctl_data.status);
#endif
			set_gemini_gpio_pin_status(ioctl_data.pin, ioctl_data.status);
			break;

		case GEMINI_GET_GPIO_PIN_STATUS:		// Get pin Status
#ifdef	ACS_DEBUG
			acs_printk("%s: GET_PIN_STATUS. pin:%d\n", __func__, ioctl_data.pin);
#endif
			ioctl_data.status = get_gemini_gpio_pin_status(ioctl_data.pin);
			if (copy_to_user((struct gemini_gpio_ioctl_data *)arg,
					&ioctl_data, sizeof(ioctl_data)))
				return -1; 
			break;
		
		case GEMINI_GPIO_RD_TEST:
			ioctl_data.status = gemini_gpio_pin_read(ioctl_data.pin, ioctl_data.use_default);
                        if (copy_to_user((struct gemini_gpio_ioctl_data *)arg, &ioctl_data, sizeof(ioctl_data)))
                                return -1;
			break;

		case GEMINI_GPIO_WR_TEST:
			gemini_gpio_pin_write(ioctl_data.pin, ioctl_data.status, ioctl_data.use_default);
			break;

		case GEMINI_WAIT_GPIO_PIN_INT:			// Wait pin Interrupt
#ifdef	ACS_DEBUG
			acs_printk("%s: GEMINI_EAIT_GPIO_PIN_INT\n", __func__);
#endif
			if(((ioctl_data.pin>31)?regist_gpio_int1:regist_gpio_int0)&((ioctl_data.pin>31)?(1<<(ioctl_data.pin-32)):(1<<ioctl_data.pin)))
				return -EBUSY;
			else
				return wait_gemini_gpio_pin_interrupt(&ioctl_data);
			
		default:
#ifdef	ACS_DEBUG
			acs_printk("%s: default\n", __func__);
#endif
			return -ENOIOCTLCMD;

	}
	return 0;
}

/************************************************************************/
/* request_gpio_irq()													*/
/* args: 	bit 	--> the pin number of gpio to regist interrupt		*/
/*			handler	--> handle of interrupt service routine				*/
/*			level	--> trigger type level/edge							*/
/*			high	--> trigger mode high/low or rigsing/falling		*/
/*			both	--> if edge trigger single/both edge				*/
/* return: 0 if succeed													*/
/*	   	   -EINVAL indicate this gpio was assigned to other module		*/
/************************************************************************/
int request_gpio_irq(int bit,void (*handler)(int),char level,char high,char both)
{
	if( (bit>=MAX_GPIO_LINE)||(bit<0) )
		return -EINVAL;

#ifdef	ACS_DEBUG
	acs_printk("%s: pin=%d\n", __func__, bit);
#endif
	
	if(bit<32){
		if(regist_gpio_int0&(1<<bit))
			return -EUSERS;
		regist_gpio_int0 |= 1<<bit;
	}
	else{
		if(regist_gpio_int1&(1<<(bit-32)))
			return -EUSERS;
		regist_gpio_int1 |= 1<<(bit-32);
	}
	
	irq_handle[bit].handler = (void *)handler;
	init_gpio_int(bit,level,high,both);
	
	return 0;
}
EXPORT_SYMBOL(request_gpio_irq);

/************************************************************************/
/* free_gpio_irq()														*/
/* args:	bit --> pin number of gpio to release irq					*/
/* return: None															*/
/************************************************************************/
int free_gpio_irq(int bit)
{
	unsigned int base,addr,data;
	
	if( (bit>=MAX_GPIO_LINE)||(bit<0) )
		return -EINVAL;
	
	base = (bit>=32) ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1;
		
	irq_handle[bit].handler = NULL;
	
	if(bit<32)
		regist_gpio_int0 &= ~(1<<bit);
	else{
		bit -= 32;
		regist_gpio_int1 &= ~(1<<bit);
	}
	
	addr = base + GPIO_INT_MASK ;
	data = readl(addr);
	data |= 1<<(bit);								// mask INT
	writel(data,addr);
	return 0;
}
EXPORT_SYMBOL(free_gpio_irq);

static struct file_operations gemini_gpio_fops = {
	.owner	=	THIS_MODULE,
	.ioctl	=	gemini_gpio_ioctl,
	.open	=	gemini_gpio_open,
	.release=	gemini_gpio_release,
};

/* GPIO_MINOR in include/linux/miscdevice.h */
static struct miscdevice gemini_gpio_miscdev =
{
	GPIO_MINOR,
	"sl_gpio",
	&gemini_gpio_fops
};

int __init gemini_gpio_init(void)
{
	int i,ret=0;

	misc_register(&gemini_gpio_miscdev);
	printk("Gemini Gpio init\n");

	for(i=0;i<MAX_GPIO_LINE;i++)
		irq_handle[i].handler = NULL;	// init handle pointer
		
	ret = request_irq(IRQ_GPIO, (void *)gpio_int1, SA_INTERRUPT, "GPIO1", NULL);
	if (ret)
		printk("Error: Register IRQ for GPIO1:%d\n",ret);
	ret = request_irq(IRQ_GPIO1, (void *)gpio_int2, SA_INTERRUPT, "GPIO2", NULL);
	if (ret)
		printk("Error: Register IRQ for GPIO2:%d\n",ret);

	for (i = 0; i < MAX_GPIO_LINE; i++) {
		init_waitqueue_head(&gemini_gpio_wait[i]);
	}
	return 0;
}	

void __exit gemini_gpio_exit(void)
{
	free_irq(IRQ_GPIO1,NULL);
	free_irq(IRQ_GPIO2,NULL);
	misc_deregister(&gemini_gpio_miscdev);
}

module_init(gemini_gpio_init);
module_exit(gemini_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jason Lee <jason@storlink.com.tw>");
MODULE_DESCRIPTION("Storlink GPIO driver");
