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
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/sl2312.h>
#include <asm/arch/irqs.h>
#include <asm/arch/gemini_gpio.h>
#if 1   // SJC
#include <linux/apm_bios.h>
#include <linux/timer.h>
#endif

#define GEMINI_GPIO_BASE1		IO_ADDRESS(SL2312_GPIO_BASE)
#define GEMINI_GPIO_BASE2		IO_ADDRESS(SL2312_GPIO_BASE1)

#define GPIO_SET	2
#define MAX_GPIO_LINE	32*GPIO_SET

#if 1	// SJC
#define PWR_LED_PIN				7

static int blink_flag = 1;
static struct timer_list led_timer;
static void deferred_notify_user(void *dummy);
void notify_user(unsigned int act);
static void blink_pwr_led(unsigned long data);
#endif

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

unsigned char led_duration[MAX_GPIO_LINE];
pid_t led_pid;
wait_queue_head_t   led_wait;
static int thread_run=0;
void led_thread(void *data)
{
	unsigned long	timeout;
	unsigned char	i;
	unsigned int reg_v0,reg_v1;
	unsigned int addr,temp,count;
	
	daemonize("LED Thread"); 
	allow_signal(SIGKILL);
	
	count = 0;
	while (1)
	{
		temp = 0;
		
		reg_v0 = readl(GEMINI_GPIO_BASE1);
		for(i=0;i<32;i++){		// GPIO0
			if(led_duration[i]==0)
				continue;
			else{
				if((count%led_duration[i])==0){
					if(reg_v0&(1<<i))		// inverse
						reg_v0 &= ~(1<<i);
					else
						reg_v0 |= (1<<i);
				}
			}
		}
		reg_v1 = readl(GEMINI_GPIO_BASE2);
		for(i=32;i<MAX_GPIO_LINE;i++){	// GPIO1
			if(led_duration[i]==0)
				continue;
			else{
				if((count%led_duration[i])==0){
					if(reg_v1&(1<<(i-32)))		// inverse
						reg_v1 &= ~(1<<(i-32));
					else
						reg_v1 |= (1<<(i-32));
				}
			}
		}
		
		for(i=0;i<MAX_GPIO_LINE;i++)
			temp += led_duration[i];
		
		if(temp==0)
			break;
			
		addr = GEMINI_GPIO_BASE1;
		writel(reg_v0,addr);
		addr = GEMINI_GPIO_BASE2;
		writel(reg_v1,addr);
		
		
		timeout = HZ/10;
		do
		{
			timeout = interruptible_sleep_on_timeout (&led_wait, timeout);
		} while (!signal_pending (current) && (timeout > 0));
		
	
		if (signal_pending (current))
		{
			//			spin_lock_irq(&current->sigmask_lock);
			flush_signals(current);
			//			spin_unlock_irq(&current->sigmask_lock);
			break;
		}
		count++;
	}
	thread_run = 0;
}


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
EXPORT_SYMBOL(set_gemini_gpio_io_mode);

// set: 1-2
// high: 1:high, 0:low
void set_gemini_gpio_pin_status(unsigned char pin, unsigned char high)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status=0,addr;
	
#if 1		// SJC
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1) + GPIO_DATA_OUT;
	status = readl(addr);

	status &= ~(1 << (pin %32));
	status |= (high << (pin % 32));
#else
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1)+(high?GPIO_DATA_SET:GPIO_DATA_CLEAR);

	status &= ~(1 << (pin %32));
	status |= (1 << (pin % 32));
#endif
	writel(status,addr);
}
EXPORT_SYMBOL(set_gemini_gpio_pin_status);

// set: 1-2
// return: 1:high, 0:low
int get_gemini_gpio_pin_status(unsigned char pin)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status,addr;
	
#if 1		// SJC
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1) + GPIO_DATA_IN;
#else
	addr = set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1;
#endif
	status = readl(addr);
	return (status&(1<<(pin%32)))?1:0; 
}
EXPORT_SYMBOL(get_gemini_gpio_pin_status);

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
	
	base = GEMINI_GPIO_BASE1;
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
		if( (regist_gpio_int0&(1<<i))&&irq_handle[i].handler ){
			irq_handle[i].handler(int_status);
		}
		else{
			wake_up(&gemini_gpio_wait[i]);				// wake up 
#if 1	// SJC
//			printk("GPIO Button %d pressed\n", i);
			notify_user((i == 1) ? 0x81 : 0x8e);
#endif
			DEB(printk("wake up gpio %d",i));
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
			irq_handle[i+32].handler(int_status);
		}
		else{
			wake_up(&gemini_gpio_wait[i+32]);				// wake up 
#if 1	// SJC
//			printk("GPIO Button %d pressed\n", i);
			notify_user((i == 1) ? 0x81 : 0x8e);
#endif
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
			set_gemini_gpio_io_mode(ioctl_data.pin, ioctl_data.status);
			break;

		case GEMINI_SET_GPIO_PIN_STATUS:		// Set pin Status
#if 1	// SJC
			if (ioctl_data.pin == PWR_LED_PIN)
				blink_flag = 0;
#endif
			set_gemini_gpio_pin_status(ioctl_data.pin, ioctl_data.status);
			break;

		case GEMINI_GET_GPIO_PIN_STATUS:		// Get pin Status
			ioctl_data.status = get_gemini_gpio_pin_status(ioctl_data.pin);
#if 1	// SJC
			copy_to_user((struct gemini_gpio_ioctl_data *)arg,
					&ioctl_data, sizeof(ioctl_data));
#else
			if (copy_to_user((struct gemini_gpio_ioctl_data *)arg,
					&ioctl_data, sizeof(ioctl_data))) 
				return -EFAULT;
#endif
			break;
		
		case GEMINI_WAIT_GPIO_PIN_INT:			// Wait pin Interrupt
			if(((ioctl_data.pin>31)?regist_gpio_int1:regist_gpio_int0)&((ioctl_data.pin>31)?(1<<(ioctl_data.pin-32)):(1<<ioctl_data.pin)))
				return -EBUSY;
			else
				return wait_gemini_gpio_pin_interrupt(&ioctl_data);
			break;
		
		case GEMINI_FLASH_GPIO_PIN:
			led_duration[ioctl_data.pin]=ioctl_data.status;
			
			if(!thread_run){		
				thread_run = 1;
				led_pid = kernel_thread ((void *)led_thread, NULL, CLONE_FS | CLONE_FILES);
				if (led_pid < 0)
    				{
	    				printk ("Unable to start LED thread\n");
    				}
			}
			break;	
		default:
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
	
#if 1	// SJC
	if (1) {
		int		i;
		struct tagGPIO {
			__u32		pin;
			__u8		dir, intr;
		} gpio[] = { {1, 0, 1}, {2, 1, 0}, {4, 1, 0}, {6, 1, 0}, {13, 0, 0}, {14, 0, 1},
			{15, 1, 0}, {16, 1, 0}, {17, 1, 0}, {18, 1, 0} };

		for (i = 0; i < (sizeof(gpio) / sizeof(struct tagGPIO)); i++) {
			set_gemini_gpio_io_mode(gpio[i].pin, gpio[i].dir);
			if (gpio[i].dir == 0) {
				// input
				if (gpio[i].intr != 0)
					init_gpio_int(gpio[i].pin, 0, 0, 1);
			} else
				set_gemini_gpio_pin_status(gpio[i].pin, (gpio[i].pin == 15) ? 0 : 1);
		}
#if 1		// SJC
		init_timer(&led_timer);
		blink_flag = 1;
		led_timer.expires = jiffies + HZ;
		led_timer.data = 0;
		led_timer.function = blink_pwr_led;              /* timer handler */
		add_timer(&led_timer);
#endif
	}
#endif
#if 1		// SJC
#else
	memset(&led_duration,0,MAX_GPIO_LINE);
	init_waitqueue_head(&led_wait);
#endif
	return 0;
}	

#if 1	// SJC
static void deferred_notify_user(void *dummy)
{
	char *argv[4], *envp[5], time_str[33], dn_str[9];
	int i, dn = 0;
	unsigned int act = *((unsigned int *)dummy);
	struct timeval tv;

	// while x81_int or x8e_int are 0xffffffff stand for button is first time
	// pressed
	// while x81_int or x8e_int are not 0xffffffff stand for button is pressed
	// and a button released event cause this function be called.

	do_gettimeofday(&tv);

	i = 0;
	argv[i++] = "/sbin/event_handler";
	argv[i++] = ((act & 0x80) == 0) ? "pwr" : "btn";
	switch (act) {
	case POWEROFF:
		argv[i++] = "POWEROFF";
		break;
	case SYSTEM_REBOOT:
		argv[i++] = "SYSTEM_REBOOT";
		break;
	case RESTORE_DEFAULT:
		argv[i++] = "RESTORE_DEFAULT";
		break;
	case 0x81:		// RELEASE_BUTTON
		dn = get_gemini_gpio_pin_status(1);
		argv[i++] = "RELEASE_BUTTON";
		break;
	case 0x8e:		// COPY
		dn = get_gemini_gpio_pin_status(14);
		argv[i++] = "COPY";
		break;
	}
	sprintf(time_str, "%u", tv.tv_sec);
	sprintf(dn_str, "%u", dn);
	argv[i++] = time_str;
	argv[i++] = dn_str;
	argv[i] = NULL;

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[i] = NULL;

	call_usermodehelper(argv[0], argv, envp, 0);

	kfree(dummy);
}

void notify_user(unsigned int act)
{
	static DECLARE_WORK(event_work, deferred_notify_user, NULL);
	unsigned int	*pact;

	if ((pact = (unsigned int*)kzalloc(sizeof(unsigned int), GFP_KERNEL)) == NULL)
		return;
	*pact = act;
	event_work.data = (void *)pact;
	schedule_work(&event_work);
}

static void blink_pwr_led(unsigned long data)
{
	if (blink_flag == 0)
		set_gemini_gpio_pin_status(PWR_LED_PIN, 0);
	else {
		set_gemini_gpio_pin_status(PWR_LED_PIN, data);
		led_timer.expires = jiffies + HZ;
		led_timer.data = (data == 0) ? 1 : 0;
		add_timer(&led_timer);
	}
}
#endif

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

