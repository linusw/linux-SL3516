/**
 * 	Accusys NAS configuration header file
 */

#ifndef __ACS_NAS_H__
#define __ACS_NAS_H__

#include <linux/types.h>

/* vendors and model */
#define ORION_430ST
#define MODEL_ORION_430ST       1

/* Orion 430ST 4 disks */
#ifdef  MODEL_ORION_430ST
        #define DISK_NUM        4
        #define NAS_MODEL       MODEL_ORION_430ST
#endif

#define THECUS_N299       1 //define sth just for N299 

extern int sl2312_led_ioctl(unsigned char, int);
extern void hdd_status_led(unsigned char, int, int);
extern void hdd_access_led(void *, int);

/* configuration */
#define CONFIG_ACS_DRIVERS_LCD 1
#define CONFIG_ACS_DRIVERS_NAND 1
#define CONFIG_ACS_DRIVERS_NVRAM 1
#define CONFIG_ACS_DRIVERS_HOTSWAP 1
#define CONFIG_ACS_DRIVERS_HEALTH 1
#define CONFIG_ACS_DRIVERS_ACPI 1
#define CONFIG_ACS_DRIVERS_KERNELLOG 1
#define CONFIG_ACS_DRIVERS_ROOTFS 1
#define CONFIG_ACS_DRIVERS_E100 1
#define CONFIG_ACS_DRIVERS_E1000 1

/* kernel debug definition */
//#define ACS_DEBUG	1
#ifdef ACS_DEBUG
	#define acs_printk(fmt, args...)	printk("[Accusys]: " fmt, ## args)
#endif

/**
 * 	Boot from nand flash 							
 *
 * 	[arch/i386/boot/compressed/misc.c]
 * 	[arch/i386/kernel/head.S]
 * 	[arch/i386/kernel/setup.c]
 * 	[drivers/block/rd.c]
 */
#define CONFIG_ACS_NAND_BOOT
#ifdef CONFIG_ACS_NAND_BOOT

	/**
	 * 	ACS_COM1	-	Accusys console com port
	 *
	 * 	[arch/i386/boot/compressed/misc.c] 
	 * 	[drivers/char/serial.c]
	 * 	change com1 baud rate to 115200 bps
	 */
	#define ACS_COM1
	/**
	 *	ACS_LOW_BUF_END	-	hard code the low buffer end
	 *
	 * 	[arch/i386/boot/compressed/misc.c]
	 */
	#define ACS_LOW_BUF_END	0x90000

	/**
	 * 	ACS_REBOOT	-	Accusys reboot for intel 815 chipset
	 *
	 * 	[arch/i386/kernel/process.c] 
	 */
	#define ACS_REBOOT

	/**
	 *	ACS_POWER_OFF	-	Accusys power off for intel 815 chipset
	 *
	 * 	[arch/i386/kernel/process.c] 
	 */
	#define ACS_POWER_OFF

	#define ACS_ACPI_PORT	0x400

	/** 
	 *	ACS_SET_ROOT_DEV	-	set root filesystem dev to ram0
	 *
	 * 	[init/main.c]
	 * 	set root dev, (1, 0) = ram0 
	 */
	#define ACS_SET_ROOT_DEV() ROOT_DEV = MKDEV(1, 0)

	/** 
	 * 	ACS_SET_RD_SOURCE	-	set rootfs image source device
	 *
	 * 	[drivers/block/rd.c] 
	 * 	source device assigned to be nand flash device
	 */
	#define ACS_SET_RD_SOURCE ROOT_DEV = MKDEV(44, 0)

	/**
	 *	ACS_ROOTFS_PANIC	- 	if rootfs gunzip error, panic
	 *
	 *	[lib/inflate.c]
	 *	while loading rootfs from nand flash to ram disk, if gunzip error
	 *	force kernel panic and output message to both console & LCD panel
	 */
	#define ACS_ROOTFS_PANIC

#endif /* CONFIG_ACS_NAND_BOOT */

/**
 * 	nfs modifications
 *
 * 	[fs/nfsd/vfs.c] 
 * 	return err early in nfsd_readlink() and nfsd_symlink() 
 * 	add nfsd_veto() for nfsd related operations 
 */
#define ACS_NFS


/**
 * 	appletalk modifications 						
 *
 * 	[net/appletalk/aarp.c] 
 * 	while bonding, ignore the aarp packet from the other ethernet device 
 * 	solve the problem that atalkd will lock rtnl_lock() while detect node 
 * 	addr 
 */
#define ACS_APPLETALK_FOR_BONDING

/**
 * 	ext2 modifications
 *
 * 	[fs/ext2/balloc.c]
 * 	[fs/ext2/ialloc.c] 
 */
#define ACS_EXT2

/**
 * 	nand char driver
 *
 * 	[drivers/char/nand.c] 
 * 	enable nand flash remap 
 *
 * 	obsolete now, since our flash already need remap function
 * 	09/03/2003
 */
//#define ACS_NAND_REMAP

/**
 * 	add support for 32MB flash
 *
 * 	[drivers/char/nand.c] [drivers/block/mybd.c]
 *
 * 	NAND_16MB: DID for SAMSUNG 16MB Flash
 * 	NAND_32MB: DID for SAMSUNG 32MB Flash
 */
#define NAND_16MB	0x73
#define NAND_32MB	0x75

/**
 * 	ACS_LCD_BEAN	-	enable lcd bean on lcd panel while booting
 *
 * 	[init/main.c] 
 */
#define ACS_LCD_BEAN

/**
 * 	ACS_LCD_MSG	-	display kernel curcial messages on LCD
 *
 * 	[init/main.c] 
 * 	[driver/block/rd.c]
 * 	[driver/block/ll_rw_blk.c] 
 */
#define ACS_LCD_MSG

/**
 * 	ide modifications
 *
 * 	[driver/ide] 
 */
//#define NAS_BOOT_SECTOR

/**
 *	[include/linux/bio.h]
 *	[include/linux/blkdev.h]
 * 	[driver/block/ll_rw_blk.c] 
 *	[driver/md/md.c]
 */
#define ACS_MD_SPECIAL

/*
 *      BAD_SECTOR_REMAP
 *      [driver/block/ll_rw_blk.c]
 *      [driver/ide/ide.c]
 *      [driver/md/md.c]
 *      [driver/md/raid0.c]
 *      [driver/md/raid1.c]
 *      [driver/md/raid5.c]
 *      [driver/char/hotswap.c]
 *
 */
//#define BAD_BLK_REMAP
//#define BAD_BLK_REMAP_TEST

/*
 *	Recollect all root file system partitions into md0 while booting.
 * 
 *	NAS_RFSP_RECOLLECT
 *	[driver/md/md.c]
 */
#define	MD0_RECOLLECTING
/*
 *	Store and identify the unique ID in SB. Mainly for distinguishing
 *	between different NAS models' disks switching. SB Version: 0.90.0
 *
 *	MD_UNIQ_IDENTIFY
 *	[driver/md/md.c]
 */
//#define   MD_UNIQ_IDENTIFY

/*
 *  Global spares(hide in md0, 24~27) support hot-spare for any MD instance
 *  which need a spare to do recovery.
 *
 *  MD_GLOBAL_SPARE
 *  [driver/md/md.c]
 *  [driver/md/raid1.c]
 *  [driver/md/raid5.c]
 */
//#define MD_GLOBAL_SPARE

/*
 *	provide periodical check(WARN) ioctl interface CHECK_STATUS
 *	for RAID 1 and 5 (!md0)
 *  
 *	MD_CHECK_STATUS
 *	[driver/md/md.c]
 *	[driver/md/raid1.c]
 *	[driver/md/raid5.c]
 *	[include/linux/raid/md_k.h]
 *	[include/linux/raid/md_u.h]
 */
#define MD_CHECK_STATUS

/**
 * 	nvram char driver							
 *
 * 	[init/main.c] 
 */
#define ACS_NAS_INFO

/**
 * 	nas timer definition 							
 *
 * 	[init/main.c]
 * 	[driver/char/health.c] 
 * 	add timer to change NAS buzzer tone
 */
#define ACCUSYS_NAS_TIMER

/**
 * 	ethernet related definition
 *
 * 	[driver/net/bonding.c]
 * 	add a bonding xmit scheme to improve load balance 
 */
#define ACS_BOND_XMIT

/* for driver/net/e100 */
#define ACCUSYS_82559
#define ACCUSYS_82559_PERFORMANCE

/* for driver/net/e1000 */
#define ACCUSYS_82540EM
#define ACCUSYS_82540EM_PERFORMANCE

#endif
