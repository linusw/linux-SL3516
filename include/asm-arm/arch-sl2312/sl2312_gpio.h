
#ifndef __GPIO_H
#define __GPIO_H

#include <linux/ioctl.h>

#define STATUS_HIGH	1
#define STATUS_LOW	0
#define DIRECT_OUT	1
#define DIRECT_IN	0

#define TRIG_EDGE	0
#define TRIG_RISE	0
#define TRIG_FALL	1
#define TRIG_SING	0
#define TRIG_BOTH	1

#define TRIG_LEVEL	1
#define TRIG_HIGH	0
#define TRIG_LOW	1


struct gpio_ioctl_data {
	unsigned int pin;
	unsigned char status;			// status or pin direction
					// 0: status low or Input
					// 1: status high or Output

	/* these member are used to config GPIO interrupt parameter */
	unsigned char	use_default;		// if not sure ,set this argument 1
	unsigned char	trig_type;		// 0/1:edge/level triger ?
	unsigned char	trig_polar;		// 0/1:rising/falling high/low active ?
	unsigned char	trig_both;		// 0/1:single/both detect both ?
};

#define GEMINI_GPIO_IOCTL_BASE	'Z'

#define IO_SET_GPIO_PIN_DIR		_IOW (GEMINI_GPIO_IOCTL_BASE,16, struct gpio_ioctl_data)
#define	IO_SET_GPIO_PIN_STATUS	_IOW (GEMINI_GPIO_IOCTL_BASE,17, struct gpio_ioctl_data)
#define	IO_GET_GPIO_PIN_STATUS	_IOWR(GEMINI_GPIO_IOCTL_BASE,18, struct gpio_ioctl_data)
#define IO_WAIT_GPIO_PIN_INT	_IOWR(GEMINI_GPIO_IOCTL_BASE,19, struct gpio_ioctl_data)


extern void init_gpio_int(__u32 pin,__u8 trig_type,__u8 trig_polar,__u8 trig_both);
extern int request_gpio_irq(int bit,void (*handler)(int),char level,char high,char both);
extern int free_gpio_irq(int bit);


#endif
