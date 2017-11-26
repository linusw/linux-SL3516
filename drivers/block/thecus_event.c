/*
 *  Copyright (C) 2006 Thecus Technology Corp. 
 *
 *      Written by Kevin Cheng (kevin_cheng@thecus.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for Thecus Event probe on Thecus N4100 / N2100
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/raid/md.h>
#include <linux/thecus_event.h>
#include <linux/acs_char.h>	//Add for kernel log and beep

#define MESSAGE_LENGTH 80
#define NORMAL_EVENT 1
#define CRITICAL_EVENT 1
#define NEDEBUG 0
#define CEDEBUG 0
#define READTEST 0
#define STOPGATE 0

#define MAX_BUFFER 50

static void clear_normal_buffer(void);
static char Normal_Message[MAX_BUFFER][MESSAGE_LENGTH];
static char Critical_Message[MAX_BUFFER][MESSAGE_LENGTH];

static int Normal_PT=0,Normal_SPT=0;
static int Critical_PT=0,Critical_SPT=0;

static wait_queue_head_t thecus_normal_event_queue;
static wait_queue_head_t thecus_critical_event_queue;

static int debug;
int				normal_event_is_open = 0;
int				critical_event_is_open = 0;
int sys_nstop=1;
int sys_cstop=1;

#ifdef DEBUG
# define _DBG(x, fmt, args...) do{ if (debug>=x) printk(KERN_DEBUG"%s: " fmt "\n", __FUNCTION__, ##args); } while(0);
#else
# define _DBG(x, fmt, args...) do { } while(0);
#endif

static spinlock_t		thecus_normal_event_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t		thecus_critical_event_lock = SPIN_LOCK_UNLOCKED;

static DECLARE_MUTEX(critical_sem);
static DECLARE_MUTEX(normal_sem);

static int critical_read;
static int normal_read;
//static DECLARE_WAIT_QUEUE_HEAD (thecus_normal_event_queue);
//static DECLARE_WAIT_QUEUE_HEAD (thecus_critical_event_queue);


#define MY_WORK_QUEUE_NAME "eventsched" // length must < 10
static void intrpt_routine(void *irrelevant);
static struct workqueue_struct *my_workqueue;
static struct work_struct Task;
static DECLARE_WORK(Task, intrpt_routine, NULL);

struct status_map {
        int status;
        char status_key[50];
};

static struct status_map the_statusmap[]={
	{RAID_STATUS_NA,RAID_NA},
	{RAID_STATUS_HEALTHY,RAID_HEALTHY},
	{RAID_STATUS_CREATE,RAID_CREATE},
	{RAID_STATUS_RECOVERY,RAID_RECOVERY},
	{RAID_STATUS_RECOVERY_HEALTHY,RAID_RECOVERY_HEALTHY},
	{RAID_STATUS_RECOVERY_FAIL,RAID_RECOVERY_FAIL},
	{RAID_STATUS_REBUILD,RAID_REBUILD},
	{RAID_STATUS_DEGRADE,RAID_DEGRADE},
	{RAID_STATUS_DAMAGE,RAID_DAMAGE},
	{RAID_STATUS_IO_FAIL,RAID_IO_FAIL},

	{0,"\0"}
};


static void intrpt_routine(void *irrelevant)
{
	if ((critical_read) && (Critical_PT!=Critical_SPT)) {
		//printk("=======================================\nCritical_PT=%d Critical_SPT=%d \n",Critical_PT,Critical_SPT);
		wake_up_interruptible(&thecus_critical_event_queue);
	}
	if ((normal_read) && (Normal_PT!=Normal_SPT)) {
		//printk("Normal_PT=%d Normal_SPT=%d \n",Critical_PT,Critical_SPT);
		wake_up_interruptible(&thecus_normal_event_queue);
	}
	
	queue_delayed_work(my_workqueue, &Task, 70);
}

int rec_critical_pt() {
	int out_pt;
	//printk("rec_critical_pt start\n");
	down(&critical_sem);
	//printk("rec_critical_pt down.....\n");
	//printk("rec_critical_pt 1\n");
	out_pt=Critical_PT++;
	//printk("rec_critical_pt 2\n");
	if (Critical_PT>=MAX_BUFFER) Critical_PT=0;
	//printk("rec_critical_pt 3\n");
	up(&critical_sem);
	//printk("rec_critical_pt up.....\n");
	//printk("rec_critical_pt stop\n");
	return out_pt;
}

int show_critical_pt() {
	int out_pt;
	down(&critical_sem);
	//printk("show_critical_pt down.....\n");

	out_pt=Critical_SPT++;
	if (Critical_SPT>=MAX_BUFFER) Critical_SPT=0;
	up(&critical_sem);
	//printk("show_critical_pt up.....\n");
	return out_pt;
}

int rec_normal_pt() {
	int out_pt;
	down(&normal_sem);
	out_pt=Normal_PT++;
	if (Normal_PT>=MAX_BUFFER) Normal_PT=0;
	up(&normal_sem);
	return out_pt;
}

int show_normal_pt() {
	int out_pt;
	down(&normal_sem);
	out_pt=Normal_SPT++;
	if (Normal_SPT>=MAX_BUFFER) Normal_SPT=0;
	up(&normal_sem);
	return out_pt;
}

void normalevent_user(char *message,char *parm1){
	if (message) {
		//down(&normal_sem);
		printk("normalevent_user: %s %s \n",message,parm1);
		sprintf(Normal_Message[rec_normal_pt()],"%s %s",message,parm1);
		//wake_up_interruptible(&thecus_normal_event_queue);
	}
}
EXPORT_SYMBOL(normalevent_user);

void criticalevent_user(char *message,char *parm1){
	if (message) {
		printk("criticalevent_user: %s %s \n",message,parm1);
		sprintf(Critical_Message[rec_critical_pt()],"%s %s",message,parm1);
		//printk("criticalevent_user end \n",message,parm1);
		//wake_up_interruptible(&thecus_critical_event_queue);
	}
}
EXPORT_SYMBOL(criticalevent_user);

void check_raid_status(mddev_t * mddev,int status) {
	int i;
	if (mddev->raid_status!=status) {
		i=0;
		while (the_statusmap[i].status!=0) {
			if (the_statusmap[i].status == status) {
				criticalevent_user(the_statusmap[i].status_key,mdname(mddev));
				mddev->raid_status=status;
				if(i==0||i==5||i==7||i==8||i==9){
					//Write log 
					write_kernellog("%d Volume%d: All disks in RAID1 were failed.", ERROR_LOG, md_to_volume(mddev));
					//Beep
					buzzer_on(BUZZ_VOL(md_to_volume(mddev)), ACS_BUZZ_ERR);
				}
			}
			i++;
		}
	}
}

EXPORT_SYMBOL(check_raid_status);


void thecus_exit_procfs(void)
{
#if NORMAL_EVENT
	remove_proc_entry("thecus_eventn", NULL);
#endif

#if CRITICAL_EVENT
	remove_proc_entry("thecus_eventc", NULL);
#endif	
}

static int
thecus_open_normal_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&thecus_normal_event_lock);
	//printk("aaaaa\n");

	if(normal_event_is_open)
		goto out_busy;

	normal_event_is_open = 1;
#if STOPGATE
	sys_nstop=0;
#endif

	spin_unlock_irq (&thecus_normal_event_lock);
	return 0;

out_busy:
	spin_unlock_irq (&thecus_normal_event_lock);

	//printk("out\n");
	return -EBUSY;
}

static ssize_t thecus_read_normal_event (
        struct file             *file,
        char                    __user *buffer,
        size_t                  length,
        loff_t                  *ppos)
{
#if !NEDEBUG 	
	static int finished = 0;
 	//printk(KERN_DEBUG "Normal process going to sleep\n");
#if STOPGATE
 	if (sys_nstop) {
 		return 0;
 	}
#endif 	
	if (finished) {
		finished = 0;
		return 0;
	}
 	//printk(KERN_DEBUG "process %i (%s) going to sleep\n",
  //         current->pid, current->comm);
#if READTEST
 	sprintf(Normal_Message,"normal test....\n");
#else	
	down(&normal_sem);
	normal_read=1;
	up(&normal_sem);
	interruptible_sleep_on(&thecus_normal_event_queue);
#endif	
 	//printk(KERN_DEBUG "awoken \n");
 	//printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
	down(&normal_sem);
	normal_read=0;
 	//printk(KERN_DEBUG "awoken \n");
	int i,show_pt;
	if (Normal_Message[Normal_SPT][0]==0) {
		up(&normal_sem);
		return 0;
	}
	up(&normal_sem);
	show_pt=show_normal_pt();
	
	/*
	while (Normal_Message[show_pt][0]==0) {
		show_pt=show_normal_pt();
	}
	*/
	
	//printk("show_pt=%d  \n",show_pt);
	
	down(&normal_sem);
	for (i = 0; i < length && Normal_Message[show_pt][i]; i++)
		put_user(Normal_Message[show_pt][i], buffer + i);

	finished = 1;
	up(&normal_sem);
	return i;
#else
 	printk(KERN_DEBUG "Normal process \n");
	return 0;	
#endif	
}


static ssize_t thecus_write_normal_event(struct file *file, const char __user *buf,
			       size_t length, loff_t *ppos)
{
	char *buffer;
	int i,err;

	if (!buf || length > PAGE_SIZE)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;

	err = -EINVAL;
	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;
		
	if (!strncmp (buffer, "clear queue", strlen ("clear queue"))) {
		printk("Clear Normal Queue. \n");
		clear_normal_buffer();
	} else if (!strncmp (buffer, "show queue", strlen ("show queue"))) {
		printk("dump normal queue . \n");
		printk("=============================\n");
		for (i=0;i<MAX_BUFFER;i++) {
			if (Critical_Message[i]) printk("%d:%s \n",i,Critical_Message[i]);
		}
	} else {
		if (buffer) {
			printk("Error: Unabled Command %s \n",buffer);
		} else {
			printk("Error: NO Command %s \n",buffer);
		}
	}
		
 out:
	free_page((unsigned long)buffer);
	return err;
}

static int
thecus_close_normal_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&thecus_normal_event_lock);
	normal_event_is_open = 0;
#if STOPGATE
	sys_nstop=1;
	if (normal_read) {
		wake_up_interruptible(&thecus_critical_event_queue);
	}
#endif
	spin_unlock_irq (&thecus_normal_event_lock);
	return 0;
}


static int
thecus_open_critical_event(struct inode *inode, struct file *file)
{
	//spin_lock_irq (&thecus_critical_event_lock);

	//if(critical_event_is_open)
	//	goto out_busy;

	//critical_event_is_open = 1;
#if STOPGATE
	sys_cstop=0;
#endif	
	//spin_unlock_irq (&thecus_critical_event_lock);
	return 0;

out_busy:
	spin_unlock_irq (&thecus_critical_event_lock);
	return -EBUSY;
}

static ssize_t thecus_read_critical_event (
        struct file             *file,
        char                    __user *buffer,
        size_t                  length,
        loff_t                  *ppos)
{
#if !CEDEBUG 	
	static int finished = 0;
 	//printk(KERN_DEBUG "Critical process start\n");
#if STOPGATE
 	if (sys_cstop) {
 		return 0;
 	}
#endif 	
	if (finished) {
		finished = 0;
		return 0;
	}
#if READTEST
 	printk("critical test....\n");
#endif
	down(&critical_sem);
	critical_read=1;
	up(&critical_sem);
 	//printk(KERN_DEBUG "Critical process going to sleep\n");
	interruptible_sleep_on(&thecus_critical_event_queue);
	down(&critical_sem);
	//printk("interruptible_sleep_on wake up sys_cstop=%d \n",sys_cstop);
#if STOPGATE
 	if (sys_cstop) {
 		printk("SYS STOP \n");
 		up(&critical_sem);
		//printk("thecus_read_critical_event sys_cstop up.....\n");
 		return 0;
 	}
#endif	

	critical_read=0;
 	//printk(KERN_DEBUG "awoken \n");
	int i,show_pt;
	
	if (Critical_Message[Critical_SPT][0]==0) {
		up(&critical_sem);
		return 0;
	}
	up(&critical_sem);
	
	show_pt=show_critical_pt();
	
	//printk("show_pt=%d  \n",show_pt);
	/*
	while (Critical_Message[show_pt][0]==0) {
		show_pt=show_critical_pt();
	}
	*/
	
	down(&critical_sem);
	for (i = 0; i < length && Critical_Message[show_pt][i]; i++) {
		put_user(Critical_Message[show_pt][i], buffer + i);
	}

	memset(Critical_Message[show_pt],0,MESSAGE_LENGTH);
	finished = 1;
	up(&critical_sem);
#if READTEST
 	printk("critical thecus_read_critical_event finished....\n");
#endif
	return i;
#else 	
 	printk(KERN_DEBUG "critical process test .... \n");
	return 0;
#endif	
}

static void clear_critical_buffer() {
		int i;
		
		down(&critical_sem);
		for (i=0;i<MAX_BUFFER;i++) {
			memset(Critical_Message[i],0,MESSAGE_LENGTH);
		}
		Critical_PT=0;
		Critical_SPT=0;
		critical_read=0;
		up(&critical_sem);
}	

static void clear_normal_buffer() {
		int i;
		
		down(&normal_sem);
		for (i=0;i<MAX_BUFFER;i++) {
			memset(Normal_Message[i],0,MESSAGE_LENGTH);
		}
		Normal_PT=0;
		Normal_SPT=0;
		normal_read=0;
		up(&normal_sem);
}	

static ssize_t thecus_write_critical_event(struct file *file, const char __user *buf,
			       size_t length, loff_t *ppos)
{
	char *buffer;
	int i,err;

	if (!buf || length > PAGE_SIZE)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;

	err = -EINVAL;
	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;
		
	if (!strncmp (buffer, "clear queue", strlen ("clear queue"))) {
		printk("Clear Critical Queue. \n");
		clear_critical_buffer();
	} else if (!strncmp (buffer, "show queue", strlen ("show queue"))) {
		printk("dump critical queue . \n");
		printk("=============================\n");
		for (i=0;i<MAX_BUFFER;i++) {
			if (Critical_Message[i]) printk("%d:%s \n",i,Critical_Message[i]);
		}
	} else if (!strncmp (buffer, "stop queue", strlen ("stop queue"))) {
		printk("stop queue . \n");
		printk("=============================\n");
#if STOPGATE
		down(&critical_sem);
		sys_cstop=1;
		up(&critical_sem);
#endif
	} else {
		if (buffer) {
			printk("Error: Unabled Command %s \n",buffer);
		} else {
			printk("Error: NO Command %s \n",buffer);
		}
	}
		
 out:
	free_page((unsigned long)buffer);
	return err;
}

static int
thecus_close_critical_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&thecus_critical_event_lock);
	critical_event_is_open = 0;
#if STOPGATE
	down(&critical_sem);
	sys_cstop=1;
	printk("thecus_close_critical_event sys_cstop=%d  critical_read=%d \n",sys_cstop,critical_read);
	up(&critical_sem);
	if (critical_read) {
		printk("wake_up_interruptible \n");
		wake_up_interruptible(&thecus_critical_event_queue);
	}
	printk("STOP critical event \n");
#endif	
	spin_unlock_irq (&thecus_critical_event_lock);
	return 0;
}

static struct file_operations proc_thecus_event_normal_operations = {
	.open			= thecus_open_normal_event,
	.write    = thecus_write_normal_event,
	.read			= thecus_read_normal_event,
	.release	= thecus_close_normal_event,
};

static struct file_operations proc_thecus_event_critical_operations = {
	.open			= thecus_open_critical_event,
	.write    = thecus_write_critical_event,
	.read			= thecus_read_critical_event,
	.release 	= thecus_close_critical_event,
};


int thecus_init_procfs(void)
{
	struct proc_dir_entry *npde, *cpde;
	
	clear_critical_buffer();
	clear_normal_buffer();
	
#if NORMAL_EVENT
	init_waitqueue_head(&thecus_normal_event_queue);
	npde = create_proc_entry("thecus_eventn", S_IRUSR, NULL);
	if (!npde)
	  return -ENOMEM;
	npde->proc_fops = &proc_thecus_event_normal_operations;
#endif

#if CRITICAL_EVENT
	init_waitqueue_head(&thecus_critical_event_queue);
	cpde = create_proc_entry("thecus_eventc", S_IRUSR, NULL);
	if (!cpde)
	  return -ENOMEM;
	cpde->proc_fops = &proc_thecus_event_critical_operations;
#endif
	//normalevent_user("Normal Event Start ... \n");

	//criticalevent_user("Critical Event Start ... \n");

	return 0;

}

static __init int thecus_event_init(void)
{
  int ret=0;
  
  printk("Thecus : Init thecus event proc entry . \n");
  
  if( thecus_init_procfs()){
      printk(KERN_ERR "Thecus : cannot create proc entry . \n");
      ret=-ENOENT;
      return ret;
  }

	my_workqueue = create_workqueue(MY_WORK_QUEUE_NAME);
	queue_delayed_work(my_workqueue, &Task, 70);
  
  if(debug>0){
    printk("Debug level=%d\n",debug);
    _DBG(1, "Debug statement: %d", debug);
  }

  return ret;
}

static __exit void thecus_event_exit(void)
{
	cancel_delayed_work(&Task); /* no "new ones" */
	flush_workqueue(my_workqueue); /* wait till all "old ones" finished */
	destroy_workqueue(my_workqueue);
	thecus_exit_procfs();
}

module_init(thecus_event_init);
module_exit(thecus_event_exit);
