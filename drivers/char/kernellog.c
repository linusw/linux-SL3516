/*
 * Supply log information to AP to display in UI
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

wait_queue_head_t kernellog_wqueue, kernellog_full;
#define	KER_LOG_LEN	96	//less than 100byte. AP define. 
#define	KER_LOG_NUM	128	//128 * 96 = 12KB 
char kernel_log[KER_LOG_NUM][KER_LOG_LEN];
unsigned int ker_log_num = 0, head = 0, tail = 0;
spinlock_t	log_lock;

static int kernellog_open(struct inode *inodep, struct file *filep);
static int kernellog_release(struct inode *inodep, struct file *filep);
static int kernellog_ioctl(struct inode *inodep, struct file *filep, unsigned int cmd, unsigned long arg);

static struct file_operations kernellog_fops =
{
	owner:		THIS_MODULE,
	llseek:		NULL,
	read:		NULL,
	write:		NULL,
	ioctl:		kernellog_ioctl,
	open:		kernellog_open,
	release:	kernellog_release,
};

#define	KERNELLOG_MINOR	243	

static struct miscdevice kernellog =
{
	KERNELLOG_MINOR,
	"kernellog",
	&kernellog_fops
};

static int kernellog_open(struct inode *inodep, struct file *filep)
{
	return 0;
}

static int kernellog_release(struct inode *inodep, struct file *filep)
{
	return 0;
}

#define	GET_LOG	0x0001	

static int kernellog_ioctl(struct inode *inodep, struct file *filep, unsigned int cmd, unsigned long arg)
{
	int	ret = 0;

	if (!capable(CAP_SYS_ADMIN)) {
		return -EACCES;
	}
	switch (cmd) {
	case GET_LOG:
		if (head == tail)
			sleep_on(&kernellog_wqueue);

		spin_lock(&log_lock);
		ret = copy_to_user((char *)arg, (char *)kernel_log[tail], strlen(kernel_log[tail]));
		tail = (tail+1) % KER_LOG_NUM;
		ker_log_num--;
		spin_unlock(&log_lock);

		return(ret);

	default:
		return(-EPERM);
	}
}

void 
write_kernellog(const char *fmt, ...)
{
	va_list args;
	char	buf[KER_LOG_LEN];

	memset(buf, 0, KER_LOG_LEN);

	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);

	if (strlen(buf) > KER_LOG_LEN) {
		printk("write_kernellog(): Message is too long!\n");
		return;
	}

	memset(kernel_log[head], 0, KER_LOG_LEN);
	memcpy(kernel_log[head], buf, strlen(buf));
	head = (head+1) % KER_LOG_NUM;
	ker_log_num++;

	wake_up(&kernellog_wqueue);
}

void
kernellog_initial(void)
{
	init_waitqueue_head(&kernellog_wqueue);
	init_waitqueue_head(&kernellog_full);

	spin_lock_init(&log_lock);
}

int __init kernellog_init(void)
{
	misc_register(&kernellog);
	kernellog_initial(); //Sunny 20070320

	return(0);
}

void __exit kernellog_exit(void)
{
	misc_deregister(&kernellog);
}

module_init(kernellog_init);
module_exit(kernellog_exit);

