/*
 *  arch/arm/mach-GM/include/mach/ftpmu010.h
 *
 *  Copyright (C) 2009 Faraday Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FTPMU010_H
#define __FTPMU010_H

#include <mach/platform/pmu.h>

#define PMU010_NAME "ftpmu010"
#define NAME_SZ     20


/*  MACROs for reading clock source
 */
#define PLL1_CLK_IN     ftpmu010_get_attr(ATTR_TYPE_PLL1)
#define PLL2_CLK_IN     ftpmu010_get_attr(ATTR_TYPE_PLL2)
#define PLL3_CLK_IN     ftpmu010_get_attr(ATTR_TYPE_PLL3)
#define AHB_CLK_IN      ftpmu010_get_attr(ATTR_TYPE_AHB)
#define APB_CLK_IN      ftpmu010_get_attr(ATTR_TYPE_APB)
#define CPU_CLK_IN      ftpmu010_get_attr(ATTR_TYPE_CPU)

/* PMU init function
 * Input parameters: virtual address of PMU
 * Return: 0 for success, < 0 for fail
 */
int ftpmu010_init(void __iomem  *base);

typedef enum 
{
    ATTR_TYPE_NONE = 0,
    ATTR_TYPE_PLL1,
    ATTR_TYPE_PLL2,
    ATTR_TYPE_PLL3,
    ATTR_TYPE_AHB,
    ATTR_TYPE_APB,
    ATTR_TYPE_CPU,
    ATTR_TYPE_PMUVER,
} ATTR_TYPE_T;

typedef struct
{
    char          name[NAME_SZ+1];    /* hclk, .... */    
    ATTR_TYPE_T   attr_type;          
    unsigned int  value;              
} attrInfo_t;

/* register attribute
 */
int ftpmu010_register_attr(attrInfo_t *attr);
int ftpmu010_deregister_attr(attrInfo_t *attr);
/* get attribute value
 * return value: 0 for fail, > 0 for success
 */
unsigned int ftpmu010_get_attr(ATTR_TYPE_T attr);


/* 
 * Structure for pinMux
 */
typedef struct 
{
    unsigned int  reg_off;    /* register offset from PMU base */
    unsigned int  bits_mask;  /* bits this module covers */
    unsigned int  lock_bits;  /* bits this module locked */    
    unsigned int  init_val;   /* initial value */
    unsigned int  init_mask;  /* initial mask */
} pmuReg_t;

typedef struct
{
    char        name[NAME_SZ+1];    /* module name length */
    int         num;                /* number of register entries */
    ATTR_TYPE_T clock_src;          /* which clock this module uses */
    pmuReg_t    *pRegArray;         /* register array */
} pmuRegInfo_t;

/* register/de-register the register table
 * return value:
 *  give an unique fd if return value >= 0, otherwise < 0 if fail.
 */
int ftpmu010_register_reg(pmuRegInfo_t *info);
int ftpmu010_deregister_reg(int fd);

/* lock/unlock/replace the bits in lock_bits field
 * return value:
 *  0 for success, < 0 for fail
 */
int ftpmu010_add_lockbits(int fd, unsigned int reg_off, unsigned int lock_bits);
int ftpmu010_del_lockbits(int fd, unsigned int reg_off, unsigned int unlock_bits);
int ftpmu010_update_lockbits(int fd, unsigned int reg_off, unsigned int new_lock_bits);
/* @int ftpmu010_bits_is_locked(int reg_off, unsigned int bits)
 * @Purpose: This function is used to check if the bits are locked by any module or not.
 * @Parameter:
 *   reg_off: register offset
 *   bits: the checked bits  
 * @Return: 
 *      If the any bit in bits is locked, then the returned value will be 0
 *      otherwise, -1 is returned to indicates all bits are available.
 *
 */
int ftpmu010_bits_is_locked(int fd, unsigned int reg_off, unsigned int bits);

/* PMU register read/write
 */
unsigned int ftpmu010_read_reg(unsigned int reg_off);
/* return value < 0 for fail */
int ftpmu010_write_reg(int fd, unsigned int reg_off, unsigned int val, unsigned int mask);

/* Purpose: calculate the divisor by input clock
 * Input: fd, in_clock, near
 * Output: None
 * Return: quotient if > 0, 0 for fail
 * Note: The return value will take the nearest value if near is 1. For example: 17.6 will be treated as 18, 
 *          but 17.4 will be treated as 17.
 */
unsigned int ftpmu010_clock_divisor(int fd, unsigned int in_clock, int near);

#endif	/* __FTPMU010_H */
