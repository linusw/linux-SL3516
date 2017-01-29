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

#define GEMINI_GPIO_BASE1		IO_ADDRESS(SL2312_GPIO_BASE)
#define GEMINI_GPIO_BASE2		IO_ADDRESS(SL2312_GPIO_BASE1)

#define GPIO_SET	2
#define SMBCLK_PIN           16  
#define SMBDATA_PIN          15 
#define SMBCLK  (1<<SMBCLK_PIN)
#define SMBDATA  (1<<SMBDATA_PIN)
#define SMBMASK  (SMBCLK|SMBDATA)

#define TEMPERATURE    0x00
#define CONFIGURATION  0x01
#define THY            0x02
#define TOS            0x03

#define FAN_SPEED_LOW_PIN    11
#define FAN_SPEED_HIGH_PIN   12
#define FAN_SPEED_STATUS_PIN 13


#define GET_TEMPERATURE    0
#define GET_THY            1
#define GET_TOS            2
#define GET_CONFIG         3
#define SET_THY            4
#define SET_TOS            5
#define SET_CONFIG         6
#define GET_FANSPEED      7
#define SET_FANSPEED      8
#define SET_GPIO          9
#define SELECT_HDD_LED    10
#define GET_GPIO          11

#define G751_ADDR  0x90  //1001xxxw/r

extern unsigned int hdd_led_select;

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
//////////////////////////////////////////////////////
void send_ack(unsigned int ack)
{ unsigned int *addr,value;
  addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
  value = readl(addr) | SMBMASK ; //SMBCLK:output,SMBDATA:output
  writel(value,addr);

  if(ack)
        addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
  else
        addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);

   writel(SMBDATA,addr);   /* output data */

  addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
  writel(SMBCLK,addr); /* set clock to 1 */
  udelay(1);

  addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);
  writel(SMBCLK,addr); /* set clock to 0 */
 
}
//////////////////////////////////////////////////////
// PIN_DIR 0:input, 1:output                  _
////////////////////////////////////////    _| |_ ,the wave like this
unsigned int read_byte(void) // read bit from G751
{
  unsigned int *addr,i,value,tmp=0;
   

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
    value = readl(addr) & ~SMBMASK|SMBCLK ; //SMBCLK:output,SMBDATA:input
    writel(value,addr);
    
   
    for(i=0;i<8;i++)
   {addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
    writel(SMBCLK,addr); /* set  to 1 */     //SMBDATA is valid when SMBCLK is high
    udelay(1);

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_DATA_IN);
    value = readl(addr);
    value = (value & (1<<SMBDATA_PIN)) >>SMBDATA_PIN;

    tmp=tmp<<1;
    if(value)
      tmp |=0x01;

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);
    writel(SMBCLK,addr); /* set  to 0 */
    udelay(1);
   }

    return tmp;

}
//////////////////////////////////////////////////////
unsigned int  write_byte(unsigned int tmp) // write bit to G751
{

  unsigned int *addr,i;
  unsigned int ack,val;

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
    val  = readl(addr) | SMBMASK ; //SMBDATA output ,SMBCLOCK output
    writel(val,addr);

    for(i=0;i<8;i++)
      {val=(tmp>>(7-i))&0x01;
       if(val)
         addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
       else
         addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);

       writel(SMBDATA,addr);   /* output data */
 
       udelay(1);
       addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
       writel(SMBCLK,addr); /* set clock to 1 */
       udelay(1);

       addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);
       writel(SMBCLK,addr); /* set clock to 0 */
       udelay(1);
      }

     addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
     val = readl(addr) & ~SMBMASK|SMBCLK ; //SMBCLK:output,SMBDATA:input,to receive ack signal
     writel(val,addr);

     addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);  //receive ack from g751
     writel(SMBDATA,addr);
     udelay(1);
     addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
     writel(SMBCLK,addr);
     udelay(1);

     addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_DATA_IN);
     ack  = readl(addr);
     ack  = (val & (1<<SMBDATA_PIN)) >>SMBDATA_PIN;

     addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR);
     writel(SMBCLK,addr); /* set clock to 0 */
     udelay(1);
     // printk("received ack %d",ack);
     return ack;
 
}
////////////////////////////////////////////////////////////////////////////
void  start(void)
{ unsigned int *addr;
    unsigned int value;

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
    value = readl(addr) | SMBMASK ; //set output mode
    writel(value,addr);

     addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
        writel(SMBCLK,addr); /* set clock to 1 */
       
	addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);/*data from 1 to 0*/
        writel(SMBDATA,addr);
        
	udelay(1);

	addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR); //data falling edge
        writel(SMBDATA,addr); 
        udelay(1);
        
       	addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR); //clock falling edge
        writel(SMBCLK,addr); 
        udelay(1);

}   
    

//////////////////////////////////////////////
void  stop(void)
{ unsigned int *addr;
    unsigned int value;

    addr = (unsigned int *)(GEMINI_GPIO_BASE1 + GPIO_PIN_DIR);
    value = readl(addr) | SMBMASK; 
    writel(value,addr);
   

    addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_CLEAR); /*data from 0 to 1 */
    writel(SMBDATA,addr);
    udelay(1);

    addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
    writel(SMBCLK,addr); /* set clock to 1 */
    udelay(1);
   
    addr = (GEMINI_GPIO_BASE1 + GPIO_DATA_SET);
    writel(SMBDATA,addr);
}   
//////////////////////////////////////////////
//there are 2 setps
//1: set the correspond pointer(subaddress)
//2: read  
unsigned int g751_read(unsigned int address,unsigned int pointer)
{ unsigned int tmp=0,tmp_msb=0,tmp_lsb=0;
  //unsigned char ack=1;//not ack,vanghan, if sda=0 when sclk=1,then is ack
  if(pointer!=TOS && pointer!=THY && pointer!=TEMPERATURE && pointer!=CONFIGURATION)
    return 0xffffffff; //error

   start();
   write_byte(address);
   write_byte(pointer);
    
   start();
   write_byte(address|0x01); //the lsb of address 0:write 1:read

  tmp_msb=read_byte();
  // printk("tmp_msb is %x\n",tmp_msb);
  if(pointer!=CONFIGURATION)
    {send_ack(0);
     tmp_lsb=read_byte();
     // printk("tmp_lsb is %x\n",tmp_lsb);
    }
  send_ack(1);
  stop();
  // printk("tmp_msb is %x\n",tmp_msb);
  // printk("tmp_msb is %x\n",tmp_lsb);
   
    // printk("xxx is %x\n",tmp_msb<<8);
   
    tmp=(tmp_msb<<8)+tmp_lsb;
    // printk("tmp is %x\n",tmp);
  if(pointer!=CONFIGURATION)
    tmp=tmp>>7;
  return tmp;
}
 
////////////////////////////////////////////////////////////////////////////////////////////

void  g751_write(unsigned address,unsigned int pointer,unsigned int data)
{ if(pointer==CONFIGURATION)
    data=data<<8;
  else if(pointer!=TOS &&  pointer!=THY )
    return ;

  data=data<<7;
  start(); 
  write_byte(address);
  write_byte(pointer);
  write_byte(data>>8); //send msb
  if(pointer==TOS || pointer==THY )
    write_byte(data&0x0000ffff);//send lsb
  stop();
}
  
 
////////////////////////////////////////////////////////////////////
// set: 1-2
// mode:0: input, 1: output
void set_io_mode(unsigned char pin, unsigned char mode)
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
void write_pin(unsigned char pin, unsigned char high)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status=0,addr;
	
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1)+(high?GPIO_DATA_SET:GPIO_DATA_CLEAR);

	status &= ~(1 << (pin %32));
	status |= (1 << (pin % 32));
	writel(status,addr);
}


// set: 1-2
// return: 1:high, 0:low
int read_pin(unsigned char pin)
{
	unsigned char set = pin >>5;		// each GPIO set has 32 pins
	unsigned int status,addr;
	
	addr = (set ? GEMINI_GPIO_BASE2:GEMINI_GPIO_BASE1)+GPIO_DATA_IN;
	status = readl(addr);
	return (status&(1<<pin))?1:0; 
}
//////////////////////////////////////////////////////////////////////////
void set_gpio(unsigned int data)
{unsigned char pin,light;
  pin=data>>8;
  light=data&0x01;
  set_io_mode(pin,1);
  write_pin(pin,light);
}
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
unsigned  int  get_gpio(unsigned int pin)
{  set_io_mode(pin,0);
   return read_pin(pin);
  

}  
/////////////////////////////////////////we dont care about below zero////////////////////////
//if the sensor outdata is beyond the field of temperature,then output oxff error code
unsigned int convert_tem(unsigned int data)
{char temp;
 if(data>0x01 && data<0x32)
  {temp=((data-0x01)*(250-5)/(0x32-0x01)+5)/10;
   return temp;
  }
else if(data>=0x32 && data<=0xfa)
 {temp=(data-0x32)*(125-25)/(0xfa-0x32)+25;
  return temp;
 }
else if(data>0xfa)
 return 0xff;   //error to read
else 
 return 0; 

}
//////////////////////////////////////////////
void set_fan_speed(unsigned int data)
{set_io_mode(FAN_SPEED_LOW_PIN,1);
 set_io_mode(FAN_SPEED_HIGH_PIN,1);
   write_pin(FAN_SPEED_LOW_PIN,data%2);
   write_pin(FAN_SPEED_HIGH_PIN,data/2);
 
}  
///////////////////////////////////////////////////////////
unsigned  int  get_fan_speed(void)
{ set_io_mode(FAN_SPEED_STATUS_PIN,0);
  return read_pin(FAN_SPEED_STATUS_PIN);

}  
////////////////////////////////////////////////////////////
static int g751_open(struct inode *inode, struct file *file)
{ 
	return 0;
}


static int g751_release(struct inode *inode, struct file *file)
{
	return 0;
}

//////////////////////////////////////////////////////////////
static int g751_ioctl(struct inode *inode, struct file *file,unsigned int cmd, unsigned int arg)
{ unsigned int data=0;
  
        get_user(data,(unsigned int *)arg);
        //printk("the arg is %d\n",data);
     	switch(cmd) {
	case GET_TEMPERATURE :
	  data=g751_read(G751_ADDR,TEMPERATURE);
	  data=convert_tem(data);
          put_user(data,(unsigned int *)arg);
	   break;
          
	 case SET_CONFIG : 
	   g751_write(G751_ADDR,CONFIGURATION,data);
	     break;

	 case SET_TOS: 
	   g751_write(G751_ADDR,TOS,data);
	      break;

	 case SET_THY:
	   g751_write(G751_ADDR,THY,data);
	      break;

	 case GET_CONFIG:
	   data=g751_read(G751_ADDR,CONFIGURATION);
           put_user(data,(unsigned int *)arg);
              break;

	 case GET_TOS: 
               data=g751_read(G751_ADDR,TOS);
               put_user(data,(unsigned int *)arg);
               break;

	 case GET_THY:
               data=g751_read(G751_ADDR,THY);
               put_user(data,(unsigned int *)arg);
               break;
	 case SET_FANSPEED:
	   if(data>=3)
	     {printk("set fan speed error\n");
	     return -EFAULT;
	     }
	   set_fan_speed(data);
           break;
                        
	 case GET_FANSPEED:
	   data=get_fan_speed();
	   put_user(data,(unsigned int *)arg);
            break;

	case SET_GPIO:
          set_gpio(data);
	   break;
        case GET_GPIO:
	  put_user(get_gpio(data),(unsigned int *)arg);
	  break;

	case SELECT_HDD_LED:
	  hdd_led_select=data; 
	  if(hdd_led_select==2)  //>95%,yellow led on
	    {set_gpio(0x0201);//blue led :GPIO_0.2,yellow led:GPIO_0.3
	     set_gpio(0x0300);
	     set_gpio(0x400);
	    }
	  else if(hdd_led_select==3)
	    {set_gpio(0x0301);
	     set_gpio(0x0200);
	     set_gpio(0x400);
	    }
	  else if(hdd_led_select==4)
	    {set_gpio(0x401);
	      set_gpio(0x200);
	      set_gpio(0x300);
	    }
	  else if(hdd_led_select==5)
	    {set_gpio(0x200);
	      set_gpio(0x300);
	      set_gpio(0x400);
	    }
           break;
	 default:
	    return -ENOIOCTLCMD;

	}
	return 0;
}




static struct file_operations g751_fops = {
	.owner	=	THIS_MODULE,
	.ioctl	=	g751_ioctl,
	.open	=	g751_open,
        .release =	g751_release,
};

/* GPIO_MINOR in include/linux/miscdevice.h */
static struct miscdevice g751_miscdev =
{
	TEMP_MINOR,
	"g751",
	&g751_fops
};

int __init g751_init(void)
{
  //	int i,ret=0;
	misc_register(&g751_miscdev);
	printk("g751 init\n");
	return 0;
}	

void __exit g751_exit(void)
{
	misc_deregister(&g751_miscdev);
}

module_init(g751_init);
module_exit(g751_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vanghan jimvon@tom.com");
MODULE_DESCRIPTION("Storlink G751 temperature sensor driver");

