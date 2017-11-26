/*======================================================================
  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
======================================================================*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/string.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/arch/sl2312.h>
#include <linux/mtd/kvctl.h>
#include "sl2312_flashmap.h"


//extern int parse_afs_partitions(struct mtd_info *, struct mtd_partition **);

/* the base address of FLASH control register */
#define FLASH_CONTROL_BASE_ADDR	    (IO_ADDRESS(SL2312_FLASH_CTRL_BASE))
#define SL2312_GLOBAL_BASE_ADDR     (IO_ADDRESS(SL2312_GLOBAL_BASE))
 
/* define read/write register utility */
#define FLASH_READ_REG(offset)			(__raw_readl(offset+FLASH_CONTROL_BASE_ADDR))
#define FLASH_WRITE_REG(offset,val) 	(__raw_writel(val,offset+FLASH_CONTROL_BASE_ADDR))

/* the offset of FLASH control register */
enum EMAC_REGISTER {
	FLASH_ID     	= 0x0000,
	FLASH_STATUS 	= 0x0008,
	FLASH_TYPE   	= 0x000c,
	FLASH_ACCESS	= 0x0020,
	FLASH_ADDRESS   = 0x0024,
	FLASH_DATA		= 0x0028,
	FLASH_TIMING    = 0x002c,
};	

//#define FLASH_BASE	FLASH_CONTROL_BASE_ADDR
//#define FLASH_SIZE	0x00800000 //INTEGRATOR_FLASH_SIZE

//#define FLASH_PART_SIZE 8388608

static unsigned int flash_indirect_access = 0;
static unsigned int chip_en = 0x00000000;

#ifdef CONFIG_SL2312_SHARE_PIN
void sl2312flash_enable_parallel_flash(void)
{
    unsigned int    reg_val;
    
    reg_val = readl(SL2312_GLOBAL_BASE_ADDR + 0x30);
    reg_val = reg_val & 0xfffffffd;
    writel(reg_val,SL2312_GLOBAL_BASE_ADDR + 0x30);
    return;
}

void sl2312flash_disable_parallel_flash(void)
{
    unsigned int    reg_val;
    
    reg_val = readl(SL2312_GLOBAL_BASE_ADDR + 0x30);
    reg_val = reg_val | 0x00000002;
    writel(reg_val,SL2312_GLOBAL_BASE_ADDR + 0x30);
    return;    
}
#endif
    

static struct map_info sl2312flash_map =
{
	name:		"SL2312 CFI Flash",
	size:       FLASH_SIZE,
	bankwidth:   2,
	//bankwidth:   1, //for 8 bits width
    phys:       SL2312_FLASH_BASE,
};

static struct mtd_info *mtd;
#if 0
static struct mtd_partition sl2312_partitions[] = {
	/* boot code */
	{
		name: "bootloader",
		offset: 0x00000000,
		size: 0x20000,
//		mask_flags: MTD_WRITEABLE,
	},
	/* kernel image */
	{
		name: "kerel image",
		offset: 0x00020000,
		size: 0x2E0000
	},
	/* All else is writable (e.g. JFFS) */
	{
		name: "user data",
		offset: 0x00300000,
		size: 0x00100000,
	}
};
#endif

        

static int __init sl2312flash_init(void)
{
	struct mtd_partition *parts;
	int nr_parts = 0;
	int ret;

    printk("SL2312 MTD Driver Init.......\n");
#ifdef CONFIG_SL2312_SHARE_PIN
    sl2312flash_enable_parallel_flash();      /* enable Parallel FLASH */
#endif
    FLASH_WRITE_REG(FLASH_ACCESS,0x00004000); /* parallel flash direct access mode */
    ret = FLASH_READ_REG(FLASH_ACCESS);
    if (ret == 0x00004000)
    {
        flash_indirect_access = 0;  /* parallel flash direct access */
    }
    else
    {    
        flash_indirect_access = 1;  /* parallel flash indirect access */
    }
    
	/*
	 * Also, the CFI layer automatically works out what size
	 * of chips we have, and does the necessary identification
	 * for us automatically.
	 */
#ifdef CONFIG_GEMINI_IPI
	sl2312flash_map.virt = FLASH_VBASE;//(unsigned int *)ioremap(SL2312_FLASH_BASE, FLASH_SIZE);
#else
	sl2312flash_map.virt = (unsigned int *)ioremap(SL2312_FLASH_BASE, FLASH_SIZE);
#endif
	//printk("sl2312flash_map.virt  = %08x\n",(unsigned int)sl2312flash_map.virt);

//	simple_map_init(&sl2312flash_map);

	mtd = do_map_probe("cfi_probe", &sl2312flash_map);
	if (!mtd)
	{
#ifdef CONFIG_SL2312_SHARE_PIN
        sl2312flash_disable_parallel_flash();      /* disable Parallel FLASH */
#endif
		return -ENXIO;
	}	
	mtd->owner = THIS_MODULE;
//    mtd->erase = flash_erase;
//    mtd->read = flash_read;
//    mtd->write = flash_write;

    parts = sl2312_partitions;
	nr_parts = sizeof(sl2312_partitions)/sizeof(*parts);
	ret = add_mtd_partitions(mtd, parts, nr_parts);
	/*If we got an error, free all resources.*/
	if (ret < 0) {
		del_mtd_partitions(mtd);
		map_destroy(mtd);
	}
#ifdef CONFIG_SL2312_SHARE_PIN
    sl2312flash_disable_parallel_flash();      /* disable Parallel FLASH */
#endif
    printk("SL2312 MTD Driver Init Success ......\n");
	return ret;
}

static void __exit sl2312flash_exit(void)
{
	if (mtd) {
		del_mtd_partitions(mtd);
		map_destroy(mtd);
	}
	
	if (sl2312flash_map.virt) {
	    iounmap((void *)sl2312flash_map.virt);
	    sl2312flash_map.virt = 0;
	}    
}

char chrtohex(char c)
{
  char val;
  if ((c >= '0') && (c <= '9'))
  {
    val = c - '0';
    return val;
  }
  else if ((c >= 'a') && (c <= 'f'))
  {
    val = 10 + (c - 'a');
    return val;
  }
  else if ((c >= 'A') && (c <= 'F'))
  {
    val = 10 + (c - 'A');
    return val;
  }
  printk("<1>Error number\n");
  return 0;
}

/* jenny changed from VCTL to NVRAM(Curconf) 2007.02.05 */
#define NV_SIZE         128 * 1024
#define MAC_OFF         132
#define NVR_LAN_MTU 	3137 
#define NVR_WAN_MTU  	3141

int get_vlaninfo(vlaninfo* vlan)
{
        struct mtd_info *mymtd = NULL;
        char *buf = NULL;
        char *mac;
        int i, j;
        size_t retlen;

        #ifdef CONFIG_SL2312_SHARE_PIN
        sl2312flash_enable_parallel_flash();
        #endif
        for(i=0;i<MAX_MTD_DEVICES;i++)
        {
                mymtd=get_mtd_device(NULL,i);
                if(mymtd && !strcmp(mymtd->name,"CurConf"))
                {
                        printk(KERN_EMERG "%s\n", mymtd->name);
                        break;
                }
        }
        if( i >= MAX_MTD_DEVICES)
        {
                printk(KERN_EMERG "Can't find nvram\n");
                #ifdef CONFIG_SL2312_SHARE_PIN
                sl2312flash_disable_parallel_flash();
                #endif
                return 0;
        }

        if (!mymtd | !mymtd->read)
        {
                printk(KERN_EMERG "<1> Can't read nvram\n");
                #ifdef CONFIG_SL2312_SHARE_PIN
                sl2312flash_disable_parallel_flash();
                #endif
                return 0;
        }
        buf = kmalloc(NV_SIZE, GFP_KERNEL);
        mymtd->read(mymtd, 0, NV_SIZE, &retlen, buf);
        if(retlen != NV_SIZE){
                printk(KERN_EMERG "<2> Read nvram fails\n");
                #ifdef CONFIG_SL2312_SHARE_PIN
                sl2312flash_disable_parallel_flash();
                #endif
                kfree(buf);
                return 0;
        }
        mac = buf + MAC_OFF;

        for(j=0; j<6; j++){
                vlan[0].mac[j] = *mac;
                mac++;
        }
        for(j=0; j<6; j++){
                vlan[1].mac[j] = *mac;
                mac++;
        }
      	kfree(buf);
	#ifdef CONFIG_SL2312_SHARE_PIN
        sl2312flash_disable_parallel_flash();
        #endif

        return 1;
}

/* 2007.08.09, Neagus */
extern struct semaphore mtd_sema;
int get_cur_conf(unsigned int off, size_t len, void *buf)
{
        struct mtd_info *mymtd = NULL;
        void *cur_conf = NULL;
        int i, ret = 0;
        size_t retlen;

        for (i = 0; i < MAX_MTD_DEVICES; i++) {
                mymtd = get_mtd_device(NULL, i);
                if (mymtd && !strcmp(mymtd->name, "CurConf"))
                        break;
        }

        if(i >= MAX_MTD_DEVICES || !mymtd || !mymtd->read) {
                printk(KERN_EMERG "Can't find nvram\n");
                ret = -1;
                goto ret_from_func1;
        }

        cur_conf = kmalloc(NV_SIZE, GFP_KERNEL);
        down_interruptible(&mtd_sema);
#ifdef CONFIG_SL2312_SHARE_PIN
        mtd_lock();
#endif
        mymtd->read(mymtd, 0, NV_SIZE, &retlen, cur_conf);
        if(retlen != NV_SIZE){
                printk(KERN_EMERG "<2> Read nvram fails\n");
                ret = -1;
                goto ret_from_func2;
        }

        memcpy(buf, cur_conf + off, len);
ret_from_func2:
#ifdef CONFIG_SL2312_SHARE_PIN
        mtd_unlock();
#endif
        up(&mtd_sema);
        kfree(cur_conf);
ret_from_func1:
        put_mtd_device(mymtd);

        return ret;
}

/* 2007.08.09, Neagus */
unsigned int get_jumbo_frame_size(int port) {
        unsigned int jumbo_frame_size = 0;
        int ret;

if(port == 0){
	/* WAN MTU, eth0 */
        ret = get_cur_conf(NVR_WAN_MTU, sizeof(unsigned int), &jumbo_frame_size);
        if (ret || (jumbo_frame_size != 1500 && jumbo_frame_size != 4000 && jumbo_frame_size != 8000))
                return 1500;
        else 
                return jumbo_frame_size;
}else if(port == 1){
	/* LAN MTU, eth1 */
        ret = get_cur_conf(NVR_LAN_MTU, sizeof(unsigned int), &jumbo_frame_size);
        if (ret || (jumbo_frame_size != 1500 && jumbo_frame_size != 4000 && jumbo_frame_size != 8000))
                return 1500;
        else 
                return jumbo_frame_size;
}
}
EXPORT_SYMBOL(get_vlaninfo);
EXPORT_SYMBOL(get_jumbo_frame_size);

module_init(sl2312flash_init);
module_exit(sl2312flash_exit);

MODULE_AUTHOR("Storlink Ltd");
MODULE_DESCRIPTION("CFI map driver");
MODULE_LICENSE("GPL");
