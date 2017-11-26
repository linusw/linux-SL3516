#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/hardware.h>

//Initialize the W83627EHG through LPC
//LPC address
#define SL3516_LPC_IO_BASE              0x47800000
#define LPC_BASE                        SL3516_LPC_IO_BASE
#define LPC_IRQ_BASE                    16
//PnP configuration register
#define LPC_KEY_ADDR                    (IO_ADDRESS(LPC_BASE) + 0x2E)
#define LPC_DATA_ADDR                   (IO_ADDRESS(LPC_BASE) + 0x2F)
//Logical Device Monitor base address
#define INDEX_ADDR                      (IO_ADDRESS(LPC_BASE) + 0x295)
#define DATA_ADDR                       (IO_ADDRESS(LPC_BASE) + 0x296)
//Winbond83627EHG register
#define BANK_SEL_REG            	0x4E
#define FAN_SPD_CTL_REG3        	0x01
//Basic operation
#define READ_UINT8(addr, value)         (value = inb(addr))
#define WRITE_UINT8(addr, value)        outb(value, addr)


#define HEALTH          		"health"
#define HEALTH_MINOR    		244

//health ioctl
#define GET_HEALTH_STATUS		0x01
#define SET_FAN_SPEED_MODE              0x02
#define SET_POWER_LOSS_RESUME_MODE      0x03
#define GET_POWER_LOSS_RESUME_MODE      0x04
#define SET_BUZZER_SWITCH               0x05
//the three cases just for QC test
#define FAN_RUN_TEN_SECOND_STOP         0x06
#define SET_FAN_RUN                     0x07
#define SET_FAN_STOP                    0x08
//check system temperature 
#define CHECK_SYS_TEMPERATURE		0x09
//Just for TaiWan test
#define SET_DUTY			0x0A
#define BEEP_NOTICE_LOGIN		0x0B
#define CHECK_FAN			0x0C


//fan speed mode
#define FAN_NORMAL		0
#define FAN_HIGH		1

//when power comes up again from power lost, remain power off or power itself on
#define POWER_OFF		0  
#define POWER_ON		1

//buzzer source definitions
#define BUZZ_DISK_1      (unsigned long long)(((unsigned long long)1) << 0)
#define BUZZ_DISK_2      (unsigned long long)(((unsigned long long)1) << 1)
#define BUZZ_DISK_3      (unsigned long long)(((unsigned long long)1) << 2)
#define BUZZ_DISK_4      (unsigned long long)(((unsigned long long)1) << 3)
#define BUZZ_TEMP1       (unsigned long long)(((unsigned long long)1) << 11)
#define BUZZ_FAN1       (unsigned long long)(((unsigned long long)1) << 16)
#define BUZZ_FAN2       (unsigned long long)(((unsigned long long)1) << 17)
#define BUZZ_FAN3       (unsigned long long)(((unsigned long long)1) << 18)
#define BUZZ_FAN4       (unsigned long long)(((unsigned long long)1) << 19)
#define BUZZ_FAN5       (unsigned long long)(((unsigned long long)1) << 20)
#define BUZZ_MD_0       (unsigned long long)(((unsigned long long)1) << 29)
#define BUZZ_VOL_1      (unsigned long long)(((unsigned long long)1) << 30)

struct nas_health {
	int     fan_speed[5];
	int	fan_mode; //0,normal. 1,high
};

extern void set_fan_stop(int index);
extern void set_fan_high(int index);



