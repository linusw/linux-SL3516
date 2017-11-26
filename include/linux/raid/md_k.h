/*
   md_k.h : kernel internal structure of the Linux MD driver
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_K_H
#define _MD_K_H

#ifndef BAD_SECTOR_REMAP
#define BAD_SECTOR_REMAP
#endif /* BAD_SECTOR_REMAP */

#ifndef MD_GLOBAL_SPARE
#define MD_GLOBAL_SPARE
#endif  /* MD_GLOBAL_SPARE */

#ifndef MD_CHECK_STATUS
#define	MD_CHECK_STATUS
#endif	/* MD_CHECK_STATUS */

#ifndef MD_ADD_IN_ERROR
#define MD_ADD_IN_ERROR
#endif  /* MD_ADD_IN_ERROR */

#ifndef MD_SYNC_CONTINUOUSLY
#define MD_SYNC_CONTINUOUSLY
#endif  /* MD_SYNC_CONTINUOUSLY */

#ifndef MD_SINGLE_DISK
#define MD_SINGLE_DISK
#endif	/* MD_SINGLE_DISK */

#define MD_RESERVED       0UL
#define LINEAR            1UL
#define RAID0             2UL
#define RAID1             3UL
#define RAID5             4UL
#define TRANSLUCENT       5UL
#define HSM               6UL
#define MULTIPATH         7UL
#define RAID6		  8UL
#define	RAID10		  9UL
#define FAULTY		  10UL
/* Accusys single disk support. Luke xia, 2005/11/04 */
#ifndef MD_SINGLE_DISK
#define MAX_PERSONALITY   11UL
#else
#define SINGLE		  11UL
#define MAX_PERSONALITY	  12UL

#define LEVEL_SINGLE	  	(-10)
#endif /* MD_SINGLE_DISK */

#define	LEVEL_MULTIPATH		(-4)
#define	LEVEL_LINEAR		(-1)
#define	LEVEL_FAULTY		(-5)

#define MaxSector (~(sector_t)0)
#define MD_THREAD_NAME_MAX 14

static inline int pers_to_level (int pers)
{
	switch (pers) {
		case FAULTY:		return LEVEL_FAULTY;
		case MULTIPATH:		return LEVEL_MULTIPATH;
		case HSM:		return -3;
		case TRANSLUCENT:	return -2;
		case LINEAR:		return LEVEL_LINEAR;
		case RAID0:		return 0;
		case RAID1:		return 1;
		case RAID5:		return 5;
		case RAID6:		return 6;
		case RAID10:		return 10;
#ifdef MD_SINGLE_DISK
		case SINGLE:		return LEVEL_SINGLE;
#endif
	}
	BUG();
	return MD_RESERVED;
}

static inline int level_to_pers (int level)
{
	switch (level) {
		case LEVEL_FAULTY: return FAULTY;
		case LEVEL_MULTIPATH: return MULTIPATH;
		case -3: return HSM;
		case -2: return TRANSLUCENT;
		case LEVEL_LINEAR: return LINEAR;
		case 0: return RAID0;
		case 1: return RAID1;
		case 4:
		case 5: return RAID5;
		case 6: return RAID6;
		case 10: return RAID10;
#ifdef MD_SINGLE_DISK
		case LEVEL_SINGLE: return SINGLE;
#endif
	}
	return MD_RESERVED;
}

static inline char *mdlevel_to_mdname (int level)
{
	switch (level) {
		case LEVEL_LINEAR: return "JBOD";
		case 0: return "RAID0";
		case 1: return "RAID1";
	}
}

typedef struct mddev_s mddev_t;
typedef struct mdk_rdev_s mdk_rdev_t;

#define MAX_MD_DEVS  256	/* Max number of md dev */

/*
 * options passed in raidrun:
 */

#define MAX_CHUNK_SIZE (4096*1024)

#ifdef	MD_GLOBAL_SPARE
static inline int disk_global_spare(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_GLOBAL_SPARE);
}

static inline void mark_disk_global_spare(mdp_disk_t * d)
{
	d->state = 0;
	d->state |= (1 << MD_DISK_GLOBAL_SPARE);
}

static inline void mark_disk_nonglobal_spare(mdp_disk_t * d)
{
	d->state &= ~(1 << MD_DISK_GLOBAL_SPARE);
}
#endif  /* MD_GLOBAL_SPARE */

static inline int disk_faulty(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_FAULTY);
}

static inline int disk_active(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_ACTIVE);
}

static inline int disk_sync(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_SYNC);
}

static inline int disk_spare(mdp_disk_t * d)
{
#ifdef	MD_GLOBAL_SPARE
	return !disk_sync(d) && !disk_active(d) && !disk_faulty(d) &&
			!disk_global_spare(d);
#else 
	return !disk_sync(d) && !disk_active(d) && !disk_faulty(d);
#endif /* MD_GLOBAL_SPARE */
}

static inline int disk_removed(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_REMOVED);
}

static inline void mark_disk_faulty(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_FAULTY);
}

static inline void mark_disk_active(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_ACTIVE);
}

static inline void mark_disk_sync(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_SYNC);
}

static inline void mark_disk_spare(mdp_disk_t * d)
{
	d->state = 0;
}

static inline void mark_disk_removed(mdp_disk_t * d)
{
#ifdef	MD_GLOBAL_SPARE
	d->state = (1 << MD_DISK_FAULTY) | (1 << MD_DISK_REMOVED)
			| ( (disk_global_spare(d) ? 1 : 0 ) << MD_DISK_GLOBAL_SPARE);
#else
	d->state = (1 << MD_DISK_FAULTY) | (1 << MD_DISK_REMOVED);
#endif
}

static inline void mark_disk_inactive(mdp_disk_t * d)
{
	d->state &= ~(1 << MD_DISK_ACTIVE);
}

static inline void mark_disk_nonsync(mdp_disk_t * d)
{
	d->state &= ~(1 << MD_DISK_SYNC);
}

/*
 * MD's 'extended' device
 */
struct mdk_rdev_s
{
	struct list_head same_set;	/* RAID devices within the same set */

	sector_t size;			/* Device size (in blocks) */
	mddev_t *mddev;			/* RAID array if running */
	unsigned long last_events;	/* IO event timestamp */

	struct block_device *bdev;	/* block device handle */

	struct page	*sb_page;
	int		sb_loaded;
	sector_t	data_offset;	/* start of data in array */
	sector_t	sb_offset;
	int		preferred_minor;	/* autorun support */

	/* A device can be in one of three states based on two flags:
	 * Not working:   faulty==1 in_sync==0
	 * Fully working: faulty==0 in_sync==1
	 * Working, but not
	 * in sync with array
	 *                faulty==0 in_sync==0
	 *
	 * It can never have faulty==1, in_sync==1
	 * This reduces the burden of testing multiple flags in many cases
	 */
	int faulty;			/* if faulty do not issue IO requests */
	int in_sync;			/* device is a full member of the array */
	/* Accusys extent disks' state:
	 * -rebuild: device doing rebuilding, not in sync: faulty==0 in_sync==0
	 */
#ifdef MD_SYNC_CONTINUOUSLY
	int rebuild;			/* device is rebuilding */
#endif

	int desc_nr;			/* descriptor index in the superblock */
	int raid_disk;			/* role of device in array */

	atomic_t	nr_pending;	/* number of pending requests.
					 * only maintained for arrays that
					 * support hot removal
					 */
};

typedef struct mdk_personality_s mdk_personality_t;

struct mddev_s
{
	void				*private;
	mdk_personality_t		*pers;
	dev_t				unit;
	int				md_minor;
	struct list_head 		disks;
	int				sb_dirty;
	int				ro;

	struct gendisk			*gendisk;

	/* Superblock information */
	int				major_version,
					minor_version,
					patch_version;
	int				persistent;
	int				chunk_size;
	time_t				ctime, utime;
	int				level, layout;
	int				raid_disks;
	int				max_disks;
	sector_t			size; /* used size of component devices */
	sector_t			array_size; /* exported array size */
	__u64				events;

	char				uuid[16];

	struct mdk_thread_s		*thread;	/* management thread */
	struct mdk_thread_s		*sync_thread;	/* doing resync or reconstruct */
	sector_t			curr_resync;	/* blocks scheduled */
	unsigned long			resync_mark;	/* a recent timestamp */
	sector_t			resync_mark_cnt;/* blocks written at resync_mark */

	sector_t			resync_max_sectors; /* may be set by personality */
	/* recovery/resync flags 
	 * NEEDED:   we might need to start a resync/recover
	 * RUNNING:  a thread is running, or about to be started
	 * SYNC:     actually doing a resync, not a recovery
	 * ERR:      and IO error was detected - abort the resync/recovery
	 * INTR:     someone requested a (clean) early abort.
	 * DONE:     thread is done and is waiting to be reaped
	 */
#define	MD_RECOVERY_RUNNING	0
#define	MD_RECOVERY_SYNC	1
#define	MD_RECOVERY_ERR		2
#define	MD_RECOVERY_INTR	3
#define	MD_RECOVERY_DONE	4
#define	MD_RECOVERY_NEEDED	5

#ifdef 	MD_SYNC_CONTINUOUSLY
#define MD_RECOVERY_RECHECK	6
#endif  /* MD_SYNC_CONTINUOUSLY */
	unsigned long			recovery;

	int				in_sync;	/* know to not need resync */
	struct semaphore		reconfig_sem;
	atomic_t			active;

	int				changed;	/* true if we might need to reread partition info */
	int				degraded;	/* whether md should consider
							 * adding a spare
							 */
	int                     	raid_status;  //hot add disk, Neagus
					//MAPPING to thecus_event.h "RAID_STATUS_XXXXXX"


	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;
	sector_t			recovery_cp;
	unsigned int			safemode;	/* if set, update "clean" superblock
							 * when no writes pending.
							 */ 
	unsigned int			safemode_delay;
	struct timer_list		safemode_timer;
	atomic_t			writes_pending; 
	request_queue_t			*queue;	/* for plugging ... */

	struct list_head		all_mddevs;

	/* ---Accusys add--- */
	/* state of mddev: CLEAN, ERROR, WARN. */
	unsigned long 			state;
	int 				sync_speed;
	int 				volumeid;

/* For MD_ADD_IN_ERROR, count update superblock failure. Luke, 2005/11/03 */
#ifdef	MD_ADD_IN_ERROR
	int				update_fail; /* update sb fail count */
#endif	/* MD_ADD_IN_ERROR */

#ifdef	MD_CHECK_STATUS
	unsigned long			last_warn; /* time stamp last warning */
#endif	/* MD_CHECK_STATUS */	
};


static inline void rdev_dec_pending(mdk_rdev_t *rdev, mddev_t *mddev)
{
	int faulty = rdev->faulty;
	if (atomic_dec_and_test(&rdev->nr_pending) && faulty)
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
}

static inline void md_sync_acct(struct block_device *bdev, unsigned long nr_sectors)
{
        atomic_add(nr_sectors, &bdev->bd_contains->bd_disk->sync_io);
}

struct mdk_personality_s
{
	char *name;
	struct module *owner;
	int (*make_request)(request_queue_t *q, struct bio *bio);
	int (*run)(mddev_t *mddev);
	int (*stop)(mddev_t *mddev);
	void (*status)(struct seq_file *seq, mddev_t *mddev);
	/* error_handler must set ->faulty and clear ->in_sync
	 * if appropriate, and should abort recovery if needed 
	 */
	void (*error_handler)(mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_add_disk) (mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_remove_disk) (mddev_t *mddev, int number);
	int (*spare_active) (mddev_t *mddev);
	int (*sync_request)(mddev_t *mddev, sector_t sector_nr, int go_faster);
	int (*resize) (mddev_t *mddev, sector_t sectors);
	int (*reshape) (mddev_t *mddev, int raid_disks);
	int (*reconfig) (mddev_t *mddev, int layout, int chunk_size);

#ifdef MD_ADD_IN_ERROR
	/* Pers for removed disk back to error-array */
	int (*hot_add_disk_in_error) (mddev_t *mddev, mdk_rdev_t *rdev);
#endif  /* MD_ADD_IN_ERROR */

#ifdef	BAD_SECTOR_REMAP
	/* Pers for performing remap */
	int (*perform_remap)(mddev_t *mddev, struct bio *bio, int in_hash);
#endif	/* BAD_SECTOR_REMAP */
};

#ifdef MD_CHECK_STATUS
extern struct timezone sys_tz;
static inline long get_nowsec(void)
{
	struct timespec now = CURRENT_TIME_SEC;
	return (long)(now.tv_sec + (-1 * sys_tz.tz_minuteswest * 60));
}
#endif

static inline char * mdname (mddev_t * mddev)
{
	return mddev->gendisk ? mddev->gendisk->disk_name : "mdX";
}

extern mdk_rdev_t * find_rdev_nr(mddev_t *mddev, int nr);

/*
 * iterates through some rdev ringlist. It's safe to remove the
 * current 'rdev'. Dont touch 'tmp' though.
 */
#define ITERATE_RDEV_GENERIC(head,rdev,tmp)				\
									\
	for ((tmp) = (head).next;					\
		(rdev) = (list_entry((tmp), mdk_rdev_t, same_set)),	\
			(tmp) = (tmp)->next, (tmp)->prev != &(head)	\
		; )
/*
 * iterates through the 'same array disks' ringlist
 */
#define ITERATE_RDEV(mddev,rdev,tmp)					\
	ITERATE_RDEV_GENERIC((mddev)->disks,rdev,tmp)

/*
 * Iterates through 'pending RAID disks'
 */
#define ITERATE_RDEV_PENDING(rdev,tmp)					\
	ITERATE_RDEV_GENERIC(pending_raid_disks,rdev,tmp)

typedef struct mdk_thread_s {
	void			(*run) (mddev_t *mddev);
	mddev_t			*mddev;
	wait_queue_head_t	wqueue;
	unsigned long           flags;
	struct completion	*event;
	struct task_struct	*tsk;
	const char		*name;
} mdk_thread_t;

#define THREAD_WAKEUP  0

#define __wait_event_lock_irq(wq, condition, lock, cmd) 		\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		spin_unlock_irq(&lock);					\
		cmd;							\
		schedule();						\
		spin_lock_irq(&lock);					\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_lock_irq(wq, condition, lock, cmd) 			\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event_lock_irq(wq, condition, lock, cmd);		\
} while (0)

#endif


#undef	MD_ADD_IN_ERROR
#undef  BAD_SECTOR_REMAP
#undef  MD_GLOBAL_SPARE
#undef  MD_CHECK_STATUS
#undef  MD_SYNC_CONTINUOUSLY
