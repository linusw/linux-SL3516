#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/arch/hardware.h>
#include <asm/arch/sl2312.h>
#include <asm/arch/irqs.h>
#include <asm/delay.h>

#include <asm/arch/gemini_gpio.h>

#define LCD_SIZE   32

#define SL2312_LCD_MINOR   255

#define SPACE           0x20
#define BYTE    unsigned char

#define LCD_GLOBAL_BASE    IO_ADDRESS(SL2312_GLOBAL_BASE)

#define LCD_BASE           IO_ADDRESS(SL2312_LCD_BASE)
//offset
#define LCD_DATA_REG       IO_ADDRESS(0x44000018)
#define LCD_ACCESS_REG     IO_ADDRESS(0x44000010)
#define LCD_STATUS_REG     IO_ADDRESS(0x44000008)

/* Command */
#define CLEAR_DISPLAY               0x01
#define SET_CURSOR_HOME             0x02
#define SET_ENTRY_MODE              0x04
#define SET_DISPLAY                 0x08
#define SET_CURSOR_DISPLAY          0x10
#define SET_FUNCTION                0x20
#define SET_CG_ADDRESS              0x40
#define SET_DD_ADDRESS              0x80
/* SET_ENTRY_MODE Option */
#define CURSOR_DECREMENT            0x02
#define WITH_DISPLAY_SHIFT          0x01
/* SET_DISPLAY options */
#define DISPLAY_ON                  0x04
#define CURSOR_ON                   0x02
#define BLINK_ON                    0x01
/* SET_CURSOR_DISPLAY options */
#define DISPLAY_SHIFT               0x08    /* set to shift display, unset to move cursor */
#define MOVE_RIGHT                  0x04    /* set to move right, unset to move left */
/* SET_FUNCTION options */
#define BITS8                       0x10    /* 1, 8 bits, 0, 4 bits */
#define TWO_LINES                   0x08    /* 1, 2 lines, 0, 1 line */
#define FONTS_5X10                  0x04    /* 1, 5x10 dots; 0, 5x7 dots */

#define GPIO_BASE           IO_ADDRESS(SL2312_GPIO_BASE)
#define GPIO_LEVEL_TRIG         1
#define GPIO_EDGE_TRIG          0
#define GPIO_LOW_ACTIVE         1
#define GPIO_HIGH_ACTIVE        0
#define GPIO_FALL_ACTIVE        1
#define GPIO_RISE_ACTIVE        0
#define GPIO_BOTH_EDGE          1
#define GPIO_SINGLE_EDGE        0

#define GPIO_LCD_BOTTON1        1
#define GPIO_LCD_BOTTON2        2

enum GPIO_REG
{
    GPIO_DATA_OUT               = 0x00,
    GPIO_DATA_IN                = 0x04,
    GPIO_PIN_DIR                = 0x08,
    GPIO_BY_PASS                = 0x0C,
    GPIO_DATA_SET               = 0x10,
    GPIO_DATA_CLEAR             = 0x14,
    GPIO_PULL_ENABLE            = 0x18,
    GPIO_PULL_TYPE                      = 0x1C,
    GPIO_INT_ENABLE             = 0x20,
    GPIO_INT_RAW_STATUS         = 0x24,
    GPIO_INT_MASK_STATUS        = 0x28,
    GPIO_INT_MASK                       = 0x2C,
    GPIO_INT_CLEAR                      = 0x30,
    GPIO_INT_TRIG                       = 0x34,
    GPIO_INT_BOTH                       = 0x38,
    GPIO_INT_POLAR                      = 0x3C
};

wait_queue_head_t button_wqueue;
unsigned int    button_flag = 0;
int kernel_msg = 1;/*kernel msg allowed*/
static int bean_on = 1;
extern void lcd_bean_on(int on)
{
        bean_on = on;
}
#define BUTTON_TIMEOUT  100     /* 100 jiffies */
#define out_lcd_cmd(x)          out_lcd_cmd_4(x,1)
#define out_lcd_data(x)         out_lcd_data_4(x)

#define Lcd_Set_Address(x)      out_lcd_cmd(SET_DD_ADDRESS|(x))
#define Lcd_Clear_Display()     out_lcd_cmd(CLEAR_DISPLAY)
#define Lcd_Cursor_Home()       out_lcd_cmd(SET_CURSOR_HOME)
#define Lcd_Cursor_Left()       out_lcd_cmd(SET_CURSOR_DISPLAY)
#define Lcd_Turn_On_CB()        out_lcd_cmd(SET_DISPLAY|DISPLAY_ON|BLINK_ON)
#define Lcd_Turn_On()           out_lcd_cmd(SET_DISPLAY|DISPLAY_ON)
#define Lcd_Turn_On_C()         out_lcd_cmd(SET_DISPLAY|DISPLAY_ON|CURSOR_ON)
#define Lcd_Mode0()             out_lcd_cmd(SET_ENTRY_MODE)
#define Lcd_Mode1()
#define Lcd_Mode2()
#define Lcd_Function_Mode0()    out_lcd_cmd(SET_FUNCTION|TWO_LINES|BITS8)
#define Lcd_Function_Mode1()    out_lcd_cmd(SET_FUNCTION|TWO_LINES|BITS8|FONTS_5X10)
#define Lcd_Display_Shift_Left()        out_lcd_cmd(SET_CURSOR_DISPLAY|DISPLAY_SHIFT)
#define Lcd_Display_Shift_Right()       out_lcd_cmd(SET_CURSOR_DISPLAY|DISPLAY_SHIFT|MOVE_RIGHT)
#define In_Lcd_Address  (inb(LCD_BASE+1)&0x7f)

static int lcd_ioctl(struct inode *inodep, struct file *filep, unsigned int cmd, unsigned long arg);
static ssize_t lcd_write(struct file *file, const char *buf, size_t len, loff_t * ppos);	

	
static int lcd_open(struct inode *inode,struct file *file)
{
        return 0;
}

static int lcd_release(struct inode *inode,struct file *file)
{
        return 0;
}

static struct file_operations lcd_fops = {
        owner:   THIS_MODULE,
	ioctl:   lcd_ioctl,
	write:   lcd_write,
        open:    lcd_open,
	release: lcd_release
};

static struct miscdevice lcd_dev = {
        SL2312_LCD_MINOR,
        "lcd",
        &lcd_fops
};

static void lcd_module_soft_reset(void)
{
        unsigned int value;

        value = readl(LCD_GLOBAL_BASE + GLOBAL_RESET_REG);
        value = value | (1<<13);
        writel(value,(LCD_GLOBAL_BASE + GLOBAL_RESET_REG));
}

static void lcd_pads_enable(void)
{
        unsigned int value;

        value = readl(LCD_GLOBAL_BASE + GLOBAL_MISC_REG);
        value = value | (1<<7);
        writel(value,(LCD_GLOBAL_BASE + GLOBAL_MISC_REG));
}

unsigned int lcd_wait(int loop)
{
	unsigned int val = 0;
	
	while (loop-- != 0){
		val = inl(LCD_ACCESS_REG);

		if(!(val&0x80000000)){
			return 0;
		}
	}
	printk("val = %x\n",val);
	return val;
}

#if 0
//8-bit
static void out_lcd_cmd_8(BYTE x,int bfc)
{
        unsigned long flags;
        printk("0x%x\n",x);
        local_irq_save(flags);

        outb(x,LCD_DATA_REG);
        switch(bfc){
                case 0:
                        outl(0xc0023000,LCD_ACCESS_REG);
                        break;
                case 1:
                        outl(0xc0033000,LCD_ACCESS_REG);
                        break;
        }

        if (x & 0x80)  { udelay(40); goto out1;}
        if (x & 0x40)  { udelay(40); goto out1;}
        if (x & 0x20)  { udelay(40); goto out1;}
        if (x & 0x10)  { udelay(40); goto out1;}
        if (x & 0x08)  { udelay(40); goto out1;}
        if (x & 0x04)  { udelay(40); goto out1;}
        if (x & 0x02)  { udelay(1640); goto out1;}
        if (x & 0x01)  { udelay(1640); goto out1;}
out1:
        local_irq_restore(flags);
        if(lcd_wait(5000))
                printk("out_lcd_cmd can't complete!\n");
}
#endif

/*
 * cmd:check BF(busy flag) or not
 *	0:not check BF
 *	1:check BF
 */
//4-bit 
static void out_lcd_cmd_4(BYTE x,int bfc)
{
	unsigned long flags;
//	printk("0x%x\n",x);
	local_irq_save(flags);

	outb(x,LCD_DATA_REG);	
	switch(bfc){
		case 0:
        		outl(0xc0003000,LCD_ACCESS_REG);
			break;
		case 1:
        		outl(0xc0013000,LCD_ACCESS_REG);
			break;
	}
 
        if (x & 0x80)  { udelay(40); goto out2;}
        if (x & 0x40)  { udelay(40); goto out2;}
        if (x & 0x20)  { udelay(40); goto out2;}
        if (x & 0x10)  { udelay(40); goto out2;}
        if (x & 0x08)  { udelay(40); goto out2;}
        if (x & 0x04)  { udelay(40); goto out2;}
        if (x & 0x02)  { udelay(1640); goto out2;}
        if (x & 0x01)  { udelay(1640); goto out2;}
out2:
	local_irq_restore(flags);
//	if(lcd_wait(5000))
//		printk("out_lcd_cmd can't complete!\n");
}

#if 0
//8-bit
static void out_lcd_data_8(BYTE x)
{
        unsigned long flags;
        printk("%c\n",x);
        local_irq_save(flags);

        outb(x,LCD_DATA_REG);
        outl(0xc0037000,LCD_ACCESS_REG);  //8-bit

        udelay(46);
        local_irq_restore(flags);
        if(lcd_wait(5000))
                printk("out_lcd_data can't complete!\n");
}
#endif

//4-bit
static void out_lcd_data_4(BYTE x)
{
        unsigned long flags;
//	printk("allen:out_lcd_data:   %c\n",x);
	local_irq_save(flags);

        outb(x,LCD_DATA_REG);
        outl(0xc0017000,LCD_ACCESS_REG);    //4-bit
        udelay(46);
	
	local_irq_restore(flags);
//	if(lcd_wait(5000))
//		printk("out_lcd_data can't complete!\n");
}

static void lcd_initialize(void)
{
	out_lcd_cmd(0x28);
	out_lcd_cmd(0x08);
	out_lcd_cmd(0x01);
	out_lcd_cmd(0x06);
	out_lcd_cmd(0x0c);
	out_lcd_cmd(0x01);
	out_lcd_cmd(0x28);
}

void lcd_putc(char c, int lcd_line, int lcd_index)
{
	unsigned long flags;

	local_irq_save(flags);

        Lcd_Set_Address(lcd_line * 0x40 + lcd_index);
        out_lcd_data(c);
	
	local_irq_restore(flags);
}

void lcd_puts( int lcd_line, char *str)
{
        register int i;
        unsigned long flags;

	local_irq_save(flags);

        switch (lcd_line)
        {
          case 1:
                Lcd_Set_Address(0x00);
                break;
          case 2:
                Lcd_Set_Address(0x40);
                break;
          default:
                goto lcd_ret;
        }

        for(i=0; *str != 0; str++,i++) {
                out_lcd_data(*str);
        }

        for (; i < 16; i++) {
                out_lcd_data(SPACE);
        }
lcd_ret:
	local_irq_restore(flags);
}

void lcd_string(unsigned long type, const char *fmt, ...)
{
        va_list args;
        char    buf[100];

        if (kernel_msg == 0)
                return;

        memset(buf, 0, 32);

        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);

        lcd_puts(1, buf);
}

#define LCD_CURSOR_R            0x101
#define LCD_CURSOR_L            0x102
#define LCD_CURSOR_ON           0x103
#define LCD_CURSOR_OFF          0x104
#define LCD_CURSOR_BLINK        0x105
#define LCD_CLEAR               0x109
#define LCD_GET_BUTTON          0x10A
#define LCD_BEAN_ON             0x10B
#define LCD_BEAN_OFF            0x10C
#define LCD_KERNEL_MSG_ON       0x10D
#define LCD_KERNEL_MSG_OFF      0x10E
static int lcd_ioctl(struct inode *inodep,struct file *filep,unsigned int cmd,unsigned long arg)
{
	switch(cmd){
		case LCD_CLEAR:
                Lcd_Clear_Display();
                break;
        case LCD_CURSOR_L:
                //Lcd_Display_Shift_Left();
                Lcd_Cursor_Left();
                break;
        case LCD_CURSOR_R:
                Lcd_Display_Shift_Right();
                break;
        case LCD_CURSOR_BLINK:
                Lcd_Turn_On_CB();
                break;
        case LCD_CURSOR_ON:
                Lcd_Turn_On_C();
                break;
        case LCD_GET_BUTTON:
                sleep_on_timeout(&button_wqueue, BUTTON_TIMEOUT);
                {
                        unsigned int current_flag = button_flag;
                        button_flag = 0xF;

                        return put_user(current_flag, (long *) arg);
                }
        case LCD_BEAN_ON:
                lcd_bean_on(1);
                break;
        case LCD_BEAN_OFF:
                lcd_bean_on(0);
                break;
        case LCD_KERNEL_MSG_ON:
                kernel_msg = 1;
                break;
        case LCD_KERNEL_MSG_OFF:
                kernel_msg = 0;
                break;
        default:
                return(-EINVAL);
        }
	return 0;
}

static ssize_t lcd_write(struct file *file,const char *buf,size_t len,loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
        int i;

        printk("lcd_write: offset=0x%x, ", *ppos);
        printk("len=%d\n", len);

        if (len < 0)
                return(-EINVAL);

        if (len > LCD_SIZE - *ppos)
                len = LCD_SIZE - *ppos;

        if (down_interruptible(&inode->i_sem))
                return(-ERESTARTSYS);

        for (i = *ppos; i < *ppos + len; i++, buf++)
                lcd_putc((*buf == '\t')?0xDF: *buf, i / 16, i % 16);

        up(&inode->i_sem);
        *ppos += len;

        printk("lcd_write(): end! len = %d\n", len);
        return(len);
}

void lcd_title(void){
	lcd_puts(1,"         ");
	lcd_puts(1,"Accusys 423ST NAS");
}

#if 0
static irqreturn_t lcd_intr(int irq,void *dev_id,struct pt_regs *regs)
{
        printk("lcd_intr start\n");
	unsigned int lcd_statu;

	lcd_statu = inl(LCD_STATUS_REG);
	if(lcd_statu & 0x00010000){
		writel(lcd_statu,LCD_STATUS_REG);
        	return IRQ_HANDLED;
	}else
		return IRQ_NONE;
		
}
#endif
	
static void lcd_botton_1(int i)
{	
	unsigned int value;
	
	udelay(100);

        value = readl(GPIO_BASE + GPIO_DATA_IN);
        if((value & 0x00000002) != 0){
		printk("lcd botton 1 click\n");
		out_lcd_cmd(0x01);
	}
}	

static void lcd_botton_2(int i)
{
	unsigned int value;

	udelay(100);

	value = readl(GPIO_BASE + GPIO_DATA_IN);
	if((value & 0x00000004) != 0){
		printk("lcd botton 2 click\n");
		lcd_puts(2,"accusys");
	}
}	

static int lcd_init(void)
{
	return 0;
        int ret;
//	unsigned int id;
//	unsigned int value;

        printk("sl2312 lcd init\n");

/*
        if(request_irq(IRQ_LCD,&lcd_intr,SA_INTERRUPT,"lcd",NULL)){
                printk("ERROR:LCD requst_irq() fail!\n");
                return -1;
        }
*/

        ret = misc_register(&lcd_dev);
        if(ret){
                printk("Unable to register misc device lcd.\n");
                return ret;
        }

	lcd_module_soft_reset();
	lcd_pads_enable();
	
        lcd_initialize();
        lcd_initialize();

#if 0
	value = readl(GPIO_BASE + GPIO_PULL_ENABLE);
	value = value & 0xffffffe1;
	writel(value,GPIO_BASE + GPIO_PULL_ENABLE);

	set_gemini_gpio_io_mode(GPIO_LCD_BOTTON1,0);
	set_gemini_gpio_io_mode(GPIO_LCD_BOTTON2,0);
#endif

	ret = request_gpio_irq(GPIO_LCD_BOTTON1,lcd_botton_1,GPIO_EDGE_TRIG,GPIO_RISE_ACTIVE,GPIO_SINGLE_EDGE);
	if(ret != 0){
		printk("lcd botton1 request gpio irq fail!\n");
		BUG_ON(1);
		return -1;
	}

	ret = request_gpio_irq(GPIO_LCD_BOTTON2,lcd_botton_2,GPIO_EDGE_TRIG,GPIO_RISE_ACTIVE,GPIO_SINGLE_EDGE);
	if(ret != 0){
		printk("lcd botton2 request gpio irq fail!\n");
		BUG_ON(1);
		return -1;
	}

	lcd_puts(1,"ACCUSYS NJ");
	lcd_puts(2,"423ST");	

	return 0;
}

static void __exit lcd_exit(void)
{
	int ret;
        misc_deregister(&lcd_dev);

	ret = free_gpio_irq(GPIO_LCD_BOTTON1);
        if(ret != 0){
                printk("lcd botton1 free gpio irq fail!\n");
		BUG_ON(1);
        }

	ret = free_gpio_irq(GPIO_LCD_BOTTON2);
        if(ret != 0){
                printk("lcd botton2 free gpio irq fail!\n");
		BUG_ON(1);
        }

        printk("sl2312 lcd exit\n");
}

module_init(lcd_init);
module_exit(lcd_exit);
