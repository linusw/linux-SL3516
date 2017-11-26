/**
 * 	Accusys NAS char device header file
 */

#ifndef __ACS_CHAR_H__
#define __ACS_CHAR_H__

/** 
 *    for driver/char/sl2312_lcd.c 
 */
//static unsigned long lcd_string_type;
#define lcd_putline lcd_puts
extern void lcd_title(void);
static inline int get_lcd_cmd(char *prompt, const char * fmt, ...){return 1;}
extern void lcd_puts(int lcd_line,char *str);
extern void lcd_string(unsigned long type, const char* fmt, ...);

/** 
 *    for driver/char/kernellog.c 
 */
#define ERROR_LOG               3
#define WARN_LOG                2
#define INFO_LOG                1

extern void write_kernellog(const char * fmt, ...);

/** 
 *    for driver/char/nvram.c 
 */
#define ACCUSYS_NVRAM_ETH0_INDEX	0x1BF0
#define ACCUSYS_NVRAM_ETH1_INDEX	0x1BF6
#define ACCUSYS_NVRAM_BONDING_INDEX	0x01FE
#define ACCUSYS_NVRAM_SRVNAME_INDEX     0x0400
#define ACCUSYS_NVRAM_SRVNAME_LEN       17
#define ACCUSYS_NVRAM_WORKGROUP_INDEX   0x0411
#define ACCUSYS_NVRAM_WORKGROUP_LEN     16
#define ACCUSYS_NVRAM_IFSPEED_INDEX     0x010B

void write_buzzer_mask_to_nvram(unsigned long long buzzer_mask);
void nvram_read_bulk(char *buf, int len, loff_t nvram_index);
#define accusys_read_ethernet_mac(eth, addr)    \
        nvram_read_bulk((addr), 6, \
                        (((eth)==0)?ACCUSYS_NVRAM_ETH0_INDEX:ACCUSYS_NVRAM_ETH1_INDEX))

#define accusys_read_bonding_mode(mode) \
        nvram_read_bulk((mode), 1, ACCUSYS_NVRAM_BONDING_INDEX)

/* S Richard */
#define accusys_read_srvname(srv_name)  \
        nvram_read_bulk((srv_name), ACCUSYS_NVRAM_SRVNAME_LEN, \
                        ACCUSYS_NVRAM_SRVNAME_INDEX)

#define accusys_read_workgroup(workgroup)       \
        nvram_read_bulk((workgroup), ACCUSYS_NVRAM_WORKGROUP_LEN, \
                        ACCUSYS_NVRAM_WORKGROUP_INDEX)

#define accusys_read_if_speed(speed)    \
        nvram_read_bulk((speed), 1, ACCUSYS_NVRAM_IFSPEED_INDEX)

/**
 *    for driver/char/lcd.c 
 */
#define LCD_L0	0
#define LCD_L1	1
#define LCD_L2	2
#define LCD_BUTTON_WFC	0x80000000 /* LCD_BUTTON Wait for clear */
#define LCD_BUTTON_MASK	0x7FFFFFFF /* LCD_BUTTON MASK */

/*
 *      Buzzer Level
 */
#define ACS_BUZZ_ALL    0x00
#define ACS_BUZZ_ERR    0x01
#define ACS_BUZZ_WARN   0x02

#define BUZZ_VOL(x)     (unsigned long long)(((unsigned long long)1) << ((x)+29))
#define BUZZ_OBC        (unsigned long long)(((unsigned long long)1) << 50)
#define BUZZ_RESTORE_DEFAULT (unsigned long long)(((unsigned long long)1) << 51)

extern void w83627_buzzer_on(unsigned long long buzzer_source);
extern void w83627_buzzer_off(unsigned long long buzzer_source);
extern void force_buzzer_on(void);
extern void force_buzzer_off(void);
static void buzzer_on(unsigned long long buzz_source, int buzz_level)
{
        w83627_buzzer_on(buzz_source);
}
static void buzzer_off(unsigned long long buzz_source, int buzz_level)
{
        w83627_buzzer_off(buzz_source);
}

#endif
