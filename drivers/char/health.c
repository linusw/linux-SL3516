/*
 * For W83627EHG.
 * Sunny 2007/02/01
 */

#include "health.h"
#include <linux/acs_char.h>

wait_queue_head_t health_wqueue;
static int fan_mode = 1;//0,normal mode. 1,high mode.
int buzzer_enable = 0;//1,open beep function.  0,close beep function. 
unsigned long long buzz_on = 0;//Each bit show one beep source. 1,beep.
static int fan_status = 1;//1,fan is normal.  0,fan is failed.
static struct timer_list buzz_timer;

extern unsigned char sl2312_qc_stat(void);

/*
 * Enter PnP mode.
 */
void EnterPnP(void)
{
        WRITE_UINT8(LPC_KEY_ADDR, 0x87);
        WRITE_UINT8(LPC_KEY_ADDR, 0x87);
}

/*
 * Exit PnP mode.
 */
void ExitPnP(void)
{
        WRITE_UINT8(LPC_KEY_ADDR, 0x02);
        WRITE_UINT8(LPC_DATA_ADDR, 0x02);

}

/*
 * Select logical device.
 */
static void SelectLD(char index)
{
        WRITE_UINT8(LPC_KEY_ADDR, 0x07);
        WRITE_UINT8(LPC_DATA_ADDR, index);
}

/*
 * Configure logical device.
 */
static void ConfigPnP(char index, char data)
{
        WRITE_UINT8(LPC_KEY_ADDR, index);
        WRITE_UINT8(LPC_DATA_ADDR, data);
}

/*
 * Select a bank.
 */
void SelectBank(char index)
{
        WRITE_UINT8(INDEX_ADDR, BANK_SEL_REG);
        WRITE_UINT8(DATA_ADDR, index);
}

/*
 * Write the data to the register.
 */
void WriteReg(char index, char data)
{
        WRITE_UINT8(INDEX_ADDR, index);
        WRITE_UINT8(DATA_ADDR, data);
}

/*
 * Read the data from the register. 
 */
unsigned char ReadReg(char index)
{
	unsigned char data;
        WRITE_UINT8(INDEX_ADDR, index);
        READ_UINT8(DATA_ADDR, data);
	return data;
}

/*
 * Get the buzzer state of the source. 1,buzzer on. 0,buzzerr off.
 */
unsigned char buzzer_state(unsigned long long buzzer_source)
{
        return (((buzz_on & buzzer_source)!=0)?1:0);
}

static void buzz_timer_fn(unsigned long data)
{
	unsigned long flags;
	unsigned char temp;

	local_irq_save(flags);
      	if (buzz_on && (buzzer_enable)) {
		SelectBank(0);
       		temp = ReadReg(0x56);
       	        //Just use this bit to beep, not imply CPUFANIN is error.
       		WriteReg(0x56, temp & 0x7f);//0,beep off
       	        WriteReg(0x56, temp | 0x80);//1, beep on
       	}
       	local_irq_restore(flags);

       	buzz_timer.expires = HZ/10 + jiffies;
       	add_timer(&buzz_timer);
}
 
/*
 * Buzzer on, consider buzzer_enable
 */
void w83627_buzzer_on(unsigned long long buzzer_source)
{
      	unsigned long flags;
      	unsigned char temp;

      	local_irq_save(flags);
      	buzz_on |= buzzer_source;
        if (buzz_on && (buzzer_enable)) {
                /* we do have buzz source */
                if (buzz_timer.function == NULL) {
                        // start buzz_timer
                        init_timer(&buzz_timer);
                        buzz_timer.expires = HZ/10 + jiffies;
                        buzz_timer.data = 0;
                        buzz_timer.function = buzz_timer_fn;
                        add_timer(&buzz_timer);
                }

                SelectBank(0);
                temp = ReadReg(0x56);
                //Just use this bit to beep, not imply CPUFANIN is error.
                WriteReg(0x56, temp | 0x80);//1, beep on
        }
        local_irq_restore(flags);
}

/*
 * Buzzer off, consider buzzer_enable
 */
void w83627_buzzer_off(unsigned long long buzzer_source)
{
        unsigned char temp;

        buzz_on &= ~buzzer_source;

	if (!buzz_on && buzz_timer.function != NULL) {
                del_timer(&buzz_timer);
                buzz_timer.function = NULL;
        }

        if (!buzz_on || (!buzzer_enable)) {
        	SelectBank(0);
        	temp = ReadReg(0x56);
                WriteReg(0x56, temp & 0x7f);//0,beep off
        }
}

/*
 * Force buzzer on, don't consider the value of buzzer_enable
 */
void force_buzzer_on(void)
{
        unsigned char temp;

        SelectBank(0);
        temp = ReadReg(0x56);
        //Just use this bit to beep, not imply CPUFANIN is error.
        WriteReg(0x56, temp | 0x80);//1, beep on
}

/*
 * Force buzzer off
 */
void force_buzzer_off(void)
{
        unsigned char temp;

        SelectBank(0);
        temp = ReadReg(0x56);
        WriteReg(0x56, temp & 0x7f);//0,beep off
}

/*
 * Beep about 30 seconds. 
 */
void beep_period()
{
        if (buzzer_enable){
		w83627_buzzer_on(BUZZ_FAN4);
		set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(30 * HZ);//beep 30 seconds then turn off
		w83627_buzzer_off(BUZZ_FAN4);	
        }
}

/*
 * There are 3 temperature inputs. Define temperatures from Pin 104,103,102
 * as temperature 1,2,3. 
 */
static int get_temperature(int index)
{	
	unsigned char temp;
	
	if (index == 1) {//SYSTEMP
		temp = ReadReg(0x27);
		return (int)temp;
	} else if (index == 3) {//HDDTEMP
		SelectBank(2);
		temp = ReadReg(0x50);
		return (int)temp;
	}
	return -1;
}

/*
 * Convert the divisor value from register to the real value.
 */
static unsigned char div_conv_r(unsigned char div)
{
        switch(div){
        case 0:
                div = 1;
                break;
        case 1:
                div = 2;
                break;
        case 2:
                div = 4;
                break;
        case 3:
                div = 8;
                break;
        case 4:
                div = 16;
                break;
        case 5:
                div = 32;
                break;
        case 6:
                div = 64;
                break;
        case 7:
                div = 128;
                break;
        }

        return div;
}

static unsigned char div_conv_w(unsigned char div)
{
        switch(div){
        case 1:
                div = 0;
                break;
        case 2:
                div = 1;
                break;
        case 4:
                div = 2;
                break;
        case 8:
                div = 3;
                break;
        case 16:
                div = 4;
                break;
	case 32:
                div = 5;
                break;
        case 64:
                div = 6;
                break;
        case 128:
                div = 7;
                break;
        }

        return(div);
}

/* 
 * There are 4 divisors for 5 fans.
 */
static unsigned char divisor_read(int index)
{	
	unsigned char data1, data5;
        unsigned char div;
        unsigned char div_r;

	data1 = ReadReg(0x47);
	SelectBank(0);
	data5 = ReadReg(0x5d);
	
        div = ((data1 >> 4) & 0x3) | (((data5 >> 5) & 0x1) << 2);
        div_r = div_conv_r(div);

        if(index == 3)//divisor 3 for SYSFAN
                return div_r;

	return -1;
}

static void divisor_write(int index, unsigned char value)
{
        unsigned char div, data1, data5;

        div = div_conv_w(value);

	data1 = ReadReg(0x47);
	SelectBank(0);
	data5 = ReadReg(0x5d);

	if (index == 3) {//divisor 3 for SYSFAN
  		data1 &= 0xcf;
        	data1 |= (div & 0x3) << 4;
        	data5 &= ~(1 << 5);
		data5 |= (div >> 2) << 5;

		WriteReg(0x47, data1);
		SelectBank(0);
		WriteReg(0x5d, data5);
	}
}

/* 
 * There are 5 fan inputs. Define the inputs from Pin 58,111,112,113,119 
 * as fan 1,2,3,4,5
 */
static unsigned int get_fan_speed(int index)
{
        unsigned int  rpm = 0;
        unsigned char div, count = 0;

	if (index == 4) //SYSFAN
        	div = divisor_read(3);//divisor 3

	if (div == 0) 
		return 0;

        if (index == 4) {
		count = ReadReg(0x28);
                if (count == 0)
                        return 0;
	}
        rpm = 1350000 / (count * div);
        if (rpm == 1350000 / (255 * div)) 
		return 0;
        rpm = rpm / 2;//The special fan hardware decide.	

	return rpm;
}

/*
 * Write the duty value to control the fan speed.
 * Define the output to Pin 7,115,116,120 as control register 1,2,3,4
 */
static void duty_write(int index, unsigned char duty)
{
        if(index == 3){//control SYSFAN
		SelectBank(0);
		WriteReg(FAN_SPD_CTL_REG3, duty);
        }
}

static unsigned char duty_read(int index)
{
        unsigned char duty;

        if (index == 3){//read duty of SYSFAN
		duty = ReadReg(FAN_SPD_CTL_REG3);
        }
        return duty;
}

/*
 * Set fan in high mode.
 */
void set_fan_high(int index)
{
	if (index == 4) {//SYSFAN
		duty_write(3, 255);
	}
}

/*
 * Set fan stop.
 */
void set_fan_stop(int index)
{	
	if (index == 4) {//SYSFAN
		duty_write(3, 0);//set duty3 to zero, so SYSFAN stop
	}
}

/*
 * Set system fan speed according to the temperature.
 */
static void check_temp_set_sysfan(void)
{
        int temp1, temp3, temp;

        temp1 = get_temperature(1);//SYSTEMP
        temp3 = get_temperature(3);//HDDTEMP
        if (temp1 > temp3) {
                temp = temp1;
        } else {
                temp = temp3;
        }

	switch (temp / 10) {
        case 0:
        case 1:
                duty_write(3, 120);
                break;
        case 2:
                duty_write(3, 140);
                break;
        case 3:
                duty_write(3, 160);
                break;
        case 4:
                duty_write(3, 200);
                break;
        case 5:
        default:
                duty_write(3, 255);
	}
}

/*
 * If the whole power is lost, then the power comes up again.
 * At this time, power will remain power-off or automatically turn itself on.
 * Index 0,remain power-off. Index 1,power itself on. 
 */
static void set_power_loss_resume(int index)
{	
	unsigned char powerloss_control;
	if (index == 0) {
		// set CR E4h[6:5] as (00)
		powerloss_control = ReadReg(0xE4);
		WriteReg(0xE4, powerloss_control & 0x9F);
	} else if (index == 1) {
		//set CR E4h[6:5] as (10)
		powerloss_control = ReadReg(0xE4);
		powerloss_control |= 0x40;
		powerloss_control &= 0xDF;	
		WriteReg(0xE4, powerloss_control);
		//set CR E6h[4] as 0
		powerloss_control = ReadReg(0xE6);
		WriteReg(0xE6, powerloss_control & 0xEF);
		//set CR E7h[4] as 1
		powerloss_control = ReadReg(0xE7);
		WriteReg(0xE7, powerloss_control | 0x10);
	}
}

/*
 * Get the power loss resume mode.
 * 0: remain power off when power resumes
 * 1: atuo power on when power resumes
 */
static int get_power_loss_resume_mode(void)
{
	int	ret;
	unsigned char power_loss_resume_mode;

// for TaiWan test
                printk(KERN_EMERG"****** Get AC power resume mode*****\n");
                unsigned char test1, test2, test3;
                test1 = ReadReg(0xE4);
                if ((test1 & 0x60) == 0) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 00******\n");
                } else if ((test1 & 0x60) == 0x40) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 10******\n");
                } else if ((test1 & 0x60) == 0x20) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 01******\n");
                } else if ((test1 & 0x60) == 0x60) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 11******\n");
                } 
                test2 = ReadReg(0xE6);
                if ((test2 & 0x10) == 0) {
                        printk(KERN_EMERG"*******CRE6[4] is 0******\n");
                } else {
                        printk(KERN_EMERG"*******CRE6[4] is 1******\n");
                }
                test3 = ReadReg(0xE7);
                if ((test3 & 0x10) == 0) {
                        printk(KERN_EMERG"*******CRE7[4] is 0******\n");
                } else {
                        printk(KERN_EMERG"*******CRE7[4] is 1******\n");
                }
//for TaiWan test
	
	// read CR E4h[6:5]
	power_loss_resume_mode = ReadReg(0xE4);

	power_loss_resume_mode &= 0x40;
	if (power_loss_resume_mode == 0x00) {
		ret = 0	;
	} else
		ret = 1;
	return ret;
}

/*
 * Check temperature. 
 */
static int check_temperature(int index)
{
        int temp;

        temp = get_temperature(index);
 	//If system temperature is overheat 65, buzzer on.
	if ((temp >= 65) && (!buzzer_state(BUZZ_TEMP1))){
		write_kernellog("%d System temperatute is over 65 degree celsius!", WARN_LOG);
		w83627_buzzer_on(BUZZ_TEMP1);	
	} else if ((temp < 65) && (buzzer_state(BUZZ_TEMP1))) {
		write_kernellog("%d System temperatute has been restored to normal.", INFO_LOG);
		w83627_buzzer_off(BUZZ_TEMP1);
	}
	return temp;
}

/*
 * Check the fan speed.
 * Write log and switch beep to report the fan state.
 */
static void check_fan(int index)
{
        unsigned int fan_speed;

        fan_speed = get_fan_speed(index);
        if (fan_speed < 500) {
                write_kernellog("%d The system fan has failed!", ERROR_LOG);
                beep_period();
		fan_status = 0;
        } else if ((fan_speed >= 500) && (fan_status == 0)) {
                write_kernellog("%d The system fan is working normal.", INFO_LOG);
                fan_status = 1;
        }
}

/*
 * Set fan duty. Print fan speed and temperatures.
 * Just for TaiWan test.
 */
static void set_fan_duty(unsigned long duty)
{
	int temp1, temp3, speed;

	duty_write(3, duty);	
	speed = get_fan_speed(4);
	temp1 = get_temperature(1);//SYSTEMP
	temp3 = get_temperature(3);//HDDTEMP
	printk(KERN_EMERG"*****fan speed %d,sys temp %d,HDD temp %d\n", speed, temp1, temp3);	
}

/*
 * Initialize the Logical Device H/W Monitor
 */
static void init_W83627EHG_monitor(void)
{
	EnterPnP();
	SelectLD(0x0B);
	ConfigPnP(0x30, 0x01);
	ConfigPnP(0x60, 0x02);
	ConfigPnP(0x61, 0x95);
	ExitPnP();	
 
//for TaiWan test
                printk(KERN_EMERG"***************************************\n");
                printk(KERN_EMERG"******AC power resume test*************\n");
                unsigned char test1, test2, test3;
                test1 = ReadReg(0xE4);
                if ((test1 & 0x60) == 0) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 00******\n");
                } else if ((test1 & 0x60) == 0x40) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 10******\n");
                } else if ((test1 & 0x60) == 0x20) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 01******\n");
                } else if ((test1 & 0x60) == 0x60) {
                        printk(KERN_EMERG"*******CRE4[6:5] is 11******\n");
                } 
                test2 = ReadReg(0xE6);
                if ((test2 & 0x10) == 0) {
                        printk(KERN_EMERG"*******CRE6[4] is 0******\n");
                } else {
                        printk(KERN_EMERG"*******CRE6[4] is 1******\n");
                }
                test3 = ReadReg(0xE7);
                if ((test3 & 0x10) == 0) {
                        printk(KERN_EMERG"*******CRE7[4] is 0******\n");
                } else {
                        printk(KERN_EMERG"*******CRE7[4] is 1******\n");
                }
//for TaiWan test

	divisor_write(3, 4);
	if (sl2312_qc_stat()) {//no jumper2 , don't do QC test
		duty_write(3, 255);//SYSFAN in high mode
	}
}

/*
 * Ioctl for health
 */
static int health_ioctl(struct inode *inodep, struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct nas_health health;
	int power_loss_resume_mode, temp, i;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	
	switch (cmd) {
	case GET_HEALTH_STATUS:
		health.fan_speed[3] = get_fan_speed(4);//get SYSFAN speed
		health.fan_mode = fan_mode;//get SYSFAN mode
		ret = copy_to_user((char *)arg, &health, sizeof(health));
                return ret;

        case SET_FAN_SPEED_MODE:
                if (arg == FAN_NORMAL) {
			fan_mode = 0;
			check_temp_set_sysfan();
		} else if (arg == FAN_HIGH) {
			fan_mode = 1;
			set_fan_high(4);//set SYSFAN high mode
		}
		return 0;

        case SET_POWER_LOSS_RESUME_MODE:
		if (arg == POWER_OFF) {				
			set_power_loss_resume(0);
		} else if (arg == POWER_ON) {
			set_power_loss_resume(1);
		}
		return 0;	

        case GET_POWER_LOSS_RESUME_MODE:
		power_loss_resume_mode = get_power_loss_resume_mode();
		ret = put_user((int)power_loss_resume_mode,(int *)arg);
		return ret;	

        case SET_BUZZER_SWITCH:
		buzzer_enable = (int)arg;
 		if (buzzer_enable == 0) {
			w83627_buzzer_off(0);			
		} else if (buzzer_enable == 1) {
			w83627_buzzer_on(0);
		}
		return 0;

	case FAN_RUN_TEN_SECOND_STOP:
                set_fan_high(4);
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(10 * HZ);
                set_fan_stop(4);
		if (!buzzer_state(BUZZ_FAN4)) {
			write_kernellog("%d The system fan has failed!", ERROR_LOG);
			w83627_buzzer_on(BUZZ_FAN4);
		}
		return 0;

        case SET_FAN_RUN:
                set_fan_high(4);
		if(buzzer_state(BUZZ_FAN4)) {
			write_kernellog("%d The system fan is working normal.", INFO_LOG);
			w83627_buzzer_off(BUZZ_FAN4);
		}
		return 0;

        case SET_FAN_STOP:
                set_fan_stop(4);
		if (!buzzer_state(BUZZ_FAN4)) {
			write_kernellog("%d The system fan has failed!", ERROR_LOG);
			w83627_buzzer_on(BUZZ_FAN4);
		}
		return 0;
	
	case CHECK_SYS_TEMPERATURE://check sys temp per 3 seconds
		temp = check_temperature(1);//SYSTEMP	
		ret = put_user(temp,(int *)arg);
        	if (fan_mode == 0) {
                	check_temp_set_sysfan();
        	}
		return ret;

	case SET_DUTY: //Just for TaiWan test
		set_fan_duty(arg);
		return 0;

	case BEEP_NOTICE_LOGIN://To notice login
		for (i = 0; i < 20; i++) {
                        force_buzzer_on();
                        mdelay(100);
                        force_buzzer_off();
                }
		return 0;

	case CHECK_FAN: //check fan per half an hour 
		check_fan(4);
		return 0;

	default:
		return -EPERM;
	}
}

static int health_open(struct inode *inodep, struct file *filep)
{
        return 0;
}

static int health_release(struct inode *inodep, struct file *filep)
{
        return 0;
}

static struct file_operations health_fops =
{
        owner:          THIS_MODULE,
        llseek:         NULL,
        read:           NULL,
        write:          NULL,
        ioctl:          health_ioctl,
        open:           health_open,
        release:        health_release,
};

static struct miscdevice health =
{
        HEALTH_MINOR,
        "health",
        &health_fops
};

int __init health_init(void)
{
	init_W83627EHG_monitor();

	misc_register(&health);
	init_waitqueue_head(&health_wqueue);

        buzz_timer.function = NULL;

	return 0;
}

void __exit health_exit(void)
{
	misc_deregister(&health);
}

module_init(health_init);
module_exit(health_exit);
