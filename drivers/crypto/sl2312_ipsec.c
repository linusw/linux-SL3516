
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/completion.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/irqs.h>
#include <asm/arch/sl2312.h>
#include <asm/arch/sl2312_ipsec.h>
#include <linux/dma-mapping.h>




/*****************************
 *      const definition     *
 *****************************/
 
#define CONFIG_IPSEC_GEMINI

/* define TX/RX descriptor parameter */
#define     TX_BUF_SIZE			2048
#define     TX_DESC_NUM			20
#define     TX_BUF_TOT_LEN		(TX_BUF_SIZE * TX_DESC_NUM)
#define     RX_BUF_SIZE			32768//2048
#define     RX_DESC_NUM			8
#define     RX_BUF_TOT_LEN		(RX_BUF_SIZE * RX_DESC_NUM)

/* define EMAC base address */
#define     IPSEC_PHYSICAL_BASE_ADDR	(SL2312_SECURITY_BASE)  //0x51000000
#define     IPSEC_BASE_ADDR			    (IO_ADDRESS(IPSEC_PHYSICAL_BASE_ADDR))
#define     IPSEC_GLOBAL_BASE_ADDR      (IO_ADDRESS(SL2312_GLOBAL_BASE)) 

//#define     IPSEC_IRQ		        0x04

#define     IPSEC_MAX_PACKET_LEN    32768//2048 + 256

#define     APPEND_MODE             0 
#define     CHECK_MODE              1

#define     MIN_HW_CHECKSUM_LEN     60

/* memory management utility */
#define DMA_MALLOC(size,handle)		pci_alloc_consistent(NULL,size,handle)	
#define DMA_MFREE(mem,size,handle)	pci_free_consistent(NULL,size,mem,handle)

#define ipsec_read_reg(offset)              (readl(IPSEC_BASE_ADDR + offset))
//#define ipsec_write_reg(offset,data,mask)    writel( (ipsec_read_reg(offset)&(~mask)) |(data&mask),(IPSEC_BASE_ADDR+offset))
#define ipsec_write_reg2(offset,data)       writel(data,(unsigned int *)(IPSEC_BASE_ADDR + offset))

/* define owner bit */
enum OWN_BIT {
    CPU = 0,
    DMA	= 1
};   

typedef struct IPSEC_PACKET_S qhead;

/*****************************
 * Global Variable Declare   *
 *****************************/
struct IPSEC_TEST_RESULT_S
{
    unsigned int    auth_cmp_result;
    unsigned int    sw_pkt_len;
    unsigned char   sw_cipher[IPSEC_MAX_PACKET_LEN];
    unsigned int    hw_pkt_len;
    unsigned char   hw_cipher[IPSEC_MAX_PACKET_LEN];
} ipsec_result; 

static IPSEC_CIPHER_CBC_T       cbc;
static IPSEC_CIPHER_ECB_T       ecb;
static IPSEC_AUTH_T             auth;
static IPSEC_AUTH_T             fcs_auth;
static IPSEC_HMAC_AUTH_T        auth_hmac;
static IPSEC_CBC_AUTH_T         cbc_auth;
static IPSEC_ECB_AUTH_T         ecb_auth;
static IPSEC_CBC_AUTH_HMAC_T    cbc_auth_hmac;
static IPSEC_ECB_AUTH_HMAC_T    ecb_auth_hmac;

static IPSEC_DESCRIPTOR_T       *rx_desc_index[RX_DESC_NUM];
static unsigned int             rx_index = 0;


static struct IPSEC_PACKET_S    fcs_op; /* for tcp/ip checksum */
//static unsigned char out_packet[2048];  /* for tcp/ip checksum */

//static unsigned short   checksum;    
static IPSEC_T          *tp;
static unsigned int     tx_desc_virtual_base = 0;
static unsigned int     rx_desc_virtual_base = 0;
//static unsigned int     tx_buf_virtual_base = 0;
//static unsigned int     rx_buf_virtual_base = 0;
static qhead            *ipsec_queue,dummy[3];
//static spinlock_t	    ipsec_lock;
static spinlock_t	    ipsec_q_lock;
//static wait_queue_head_t   ipsec_wait_q;
//static unsigned int     fcs_data_len = 0;
static unsigned int wep_crc_ok = 0;
static unsigned int tkip_mic_ok = 0;
static unsigned int ccmp_mic_ok = 0;

/*************************************
 *     Function Prototype            *
 *************************************/
static void ipsec_fcs_init(void);
static void ipsec_hw_auth(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
                            unsigned char *out_pkt,int *out_len);
static void ipsec_hw_cipher(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
                            unsigned char *out_pkt,int *out_len);
static void ipsec_hw_fcs(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
                            unsigned char *out_pkt,int *out_len);
static void ipsec_byte_change(unsigned char *in_key,unsigned int in_len,unsigned char *out_key,unsigned int *out_len);
static void ipsec_rx_packet(void);
static int ipsec_tx_packet(struct scatterlist *packet, int len, unsigned int tqflag);
static void ipsec_complete_tx_packet(void);
static irqreturn_t ipsec_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int ipsec_interrupt_polling(void);
static int ipsec_auth_and_cipher(struct IPSEC_PACKET_S  *op);
static int ipsec_auth_or_cipher(struct IPSEC_PACKET_S  *op);
static void ipsec_byte_change(unsigned char *in_key,unsigned int in_len,unsigned char *out_key,unsigned int *out_len);
static void ipsec_put_queue(qhead *q,struct IPSEC_PACKET_S *item);
static struct IPSEC_PACKET_S *ipsec_get_queue(qhead *q);

/************************************************/
/*                 function body                */
/************************************************/
__inline__ unsigned int ipsec_get_time(void)
{
    return (readl(0xf2300000));
}

#if 0    
static unsigned int ipsec_read_reg(unsigned int offset)
{
    unsigned int    reg_val;
    
    reg_val = readl(IPSEC_BASE_ADDR + offset);
	return (reg_val);
}
#endif

static void ipsec_write_reg(unsigned int offset,unsigned int data,unsigned int bit_mask)
{
	unsigned int reg_val;
    unsigned int *addr;
    	
	reg_val = ( ipsec_read_reg(offset) & (~bit_mask) ) | (data & bit_mask);
	addr = (unsigned int *)(IPSEC_BASE_ADDR + offset);
    writel(reg_val,addr);
	return;
}	

void ipsec_sw_reset(void)
{
    unsigned int reg_val;
    
    reg_val = readl(IPSEC_GLOBAL_BASE_ADDR + 0x10) | 0x00000010;
    writel(reg_val,IPSEC_GLOBAL_BASE_ADDR + 0x10);
    return;
}

//void hw_memcpy(char *to, char *from, unsigned long n)
//{
//        unsigned int  i;
//        unsigned int p_to = __pa(to);
//        unsigned int    p_from = __pa(from);
//
//        consistent_sync(to,n,DMA_BIDIRECTIONAL);
//        //consistent_sync(from,n,DMA_BIDIRECTIONAL);
//        writel(p_from,IO_ADDRESS(SL2312_DRAM_CTRL_BASE)+0x24);  /* set source address */
//        writel(p_to,IO_ADDRESS(SL2312_DRAM_CTRL_BASE)+0x28);    /* set destination address */
//        writel(n,IO_ADDRESS(SL2312_DRAM_CTRL_BASE)+0x2c);     /* set byte count */
//        wmb();
//        writel(0x00000001,IO_ADDRESS(SL2312_DRAM_CTRL_BASE)+0x20);
//
//        while (readl(IO_ADDRESS(SL2312_DRAM_CTRL_BASE)+0x20)==0x00000001);
//}

static void ipsec_put_queue(qhead *q,struct IPSEC_PACKET_S *i)
{
	unsigned long flags;

	spin_lock_irqsave(&ipsec_q_lock, flags);

	i->next = q->next;
	i->prev = q;
	q->next->prev = i;
	q->next = i;

	spin_unlock_irqrestore(&ipsec_q_lock, flags);
	return;
}

 

static struct IPSEC_PACKET_S * ipsec_get_queue(qhead *q)
{
	struct IPSEC_PACKET_S *i;
	unsigned long flags;

	if(q->prev == q)
	{
		return NULL;
	}

	spin_lock_irqsave(&ipsec_q_lock, flags);
	i = q->prev;
	q->prev = i->prev;
	i->prev->next = i->next;

	spin_unlock_irqrestore(&ipsec_q_lock, flags);

	i->next = i->prev = NULL;
	return i;
}

    

/*****************************************************************************
 * Function    : ipsec_crypto_hw_process
 * Description : This function processes H/W authentication and cipher.
 *       Input : op_info - the authentication and cipher information for IPSec module.
 *      Output : none.
 *      Return : 0 - success, others - failure.
 *****************************************************************************/
int ipsec_crypto_hw_process(struct IPSEC_PACKET_S  *op_info)
{
    IPSEC_DESCRIPTOR_T  *rx_desc;
    static unsigned int pid = 0;


    op_info->process_id = (pid++) % 256;

    
    
    
#if (ZERO_COPY==1)    
    /* get rx descriptor for this operation */
    rx_desc = rx_desc_index[rx_index%RX_DESC_NUM];
    /* set receive buffer address for this operation */ 
//    consistent_sync(op_info->out_packet,op_info->pkt_len,PCI_DMA_TODEVICE);
    rx_desc->buf_adr = __pa(op_info->out_packet); //virt_to_phys(op_info->out_packet);
//	ipsec_write_reg(IPSEC_RXDMA_BUF_ADDR,rx_desc->buf_adr,0xffffffff);    
	ipsec_write_reg2(IPSEC_RXDMA_BUF_ADDR,rx_desc->buf_adr);    
    rx_index++;
#endif
//    printk("%s : ipsec_put_queue op_info->process_id=%d pkt_len=%d\n",__func__,op_info->process_id,op_info->pkt_len);
    ipsec_put_queue(ipsec_queue,op_info);

    if ((op_info->op_mode==ENC_AUTH) || (op_info->op_mode==AUTH_DEC))
    {
        ipsec_auth_and_cipher(op_info);
    }
    else
    {
        ipsec_auth_or_cipher(op_info);
    }    

    return 0;
}    

static unsigned char       iv[16];
static unsigned char       cipher_key[32];
static unsigned char       auth_key[64];
static unsigned char       auth_result[20];
    
/*======================================================================================================*/
/*    Generate random packet and do software/hardware authentication/encryption/decryption   */ 
/*======================================================================================================*/
static int ipsec_auth_or_cipher(struct IPSEC_PACKET_S  *op)
{
    unsigned int        iv_size;
	unsigned int		tdflag=0;
	unsigned int        ctrl_pkt_len;
	unsigned int        cipher_key_size;
	unsigned int        auth_key_size;
	unsigned int        auth_result_len;

    if ( (op->op_mode == CIPHER_ENC) || (op->op_mode == CIPHER_DEC) )   /* Encryption & Decryption */
    {
        if ((op->cipher_algorithm == CBC_DES) || (op->cipher_algorithm == CBC_3DES) || (op->cipher_algorithm == CBC_AES))
        {
            memset(&cbc,0x00,sizeof(IPSEC_CIPHER_CBC_T));
            cbc.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            cbc.control.bits.cipher_algorithm = op->cipher_algorithm; /* DES-CBC mode */ 
            cbc.control.bits.process_id = op->process_id;   /* set frame process id */
            cbc.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            cbc.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(cbc.cipher_key,cipher_key,cipher_key_size);
        }
        else
        {    
            memset(&ecb,0x00,sizeof(IPSEC_CIPHER_ECB_T));
            ecb.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            ecb.control.bits.cipher_algorithm = op->cipher_algorithm; /* DES-CBC mode */ 
            ecb.control.bits.process_id = op->process_id;   /* set frame process id */
            ecb.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            ecb.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(ecb.cipher_key,cipher_key,cipher_key_size);
        } 
    }
    
    if (op->op_mode == AUTH)    /* Authentication */
    {
        if ((op->auth_algorithm == MD5) || (op->auth_algorithm == SHA1) )
        {   
            memset(&auth,0x00,sizeof(IPSEC_AUTH_T));
            auth.var.control.bits.op_mode = op->op_mode;    /* authentication */
            auth.var.control.bits.auth_mode = op->auth_result_mode;  /* append/check authentication result  */
            auth.var.control.bits.auth_algorithm = op->auth_algorithm; /* MD5 */
            auth.var.control.bits.process_id = op->process_id;   /* set frame process id */
            auth.var.auth.bits.auth_header_len = op->auth_header_len;
            auth.var.auth.bits.auth_algorithm_len = op->auth_algorithm_len;
        }
#if 1
        else if (op->auth_algorithm == FCS)
        {
            fcs_auth.var.control.bits.process_id = op->process_id;   /* set frame process id */
            fcs_auth.var.auth.bits.auth_header_len = op->auth_header_len;
            fcs_auth.var.auth.bits.auth_algorithm_len = op->auth_algorithm_len;
//            fcs_auth.var.control.bits.auth_check_len = 4; /* 4-word to be checked or appended */
            ipsec_hw_fcs((unsigned char *)&fcs_auth,28,
                        op->in_packet,op->pkt_len,0x45,
                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len); 
            return 0;
        }
#endif        
        else
        {
            memset(&auth_hmac,0x00,sizeof(IPSEC_HMAC_AUTH_T));
            auth_hmac.control.bits.op_mode = op->op_mode;    /* authentication */
            auth_hmac.control.bits.auth_mode = op->auth_result_mode;  /* append/check authentication result  */
            auth_hmac.control.bits.auth_algorithm = op->auth_algorithm; /* MD5 */
            auth_hmac.control.bits.process_id = op->process_id;   /* set frame process id */
            auth_hmac.auth.bits.auth_header_len = op->auth_header_len;
            auth_hmac.auth.bits.auth_algorithm_len = op->auth_algorithm_len;
            ipsec_byte_change(op->auth_key,op->auth_key_size,auth_key,&auth_key_size);
            memcpy(auth_hmac.auth_key,auth_key,auth_key_size);
        }
    }

    switch (op->op_mode)
    {
        case CIPHER_ENC:
        	switch(op->cipher_algorithm)
        	{
            	case CBC_DES:
            	    op->iv_size = 8;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size);
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;  /* 1+2+8+10+20+40 */
#else                    
                    tdflag = 0x1b;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;
        			
                case ECB_DES:
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;  /* 1+2+10+20+40 */
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
                    break;
        			
                case CBC_3DES:
            	    op->iv_size = 8;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size); /* set IV */
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;
#else
                    tdflag = 0x1b;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
                    break;
        			
                case ECB_3DES:
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
           			break;
        			
                case CBC_AES:
                    cbc.control.bits.aesnk = op->cipher_key_size/4; /* AES key size */ 
            	    op->iv_size = 16;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size); /* set IV */
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;
#else
                    tdflag = 0x1b;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;
        			
                case ECB_AES:
                    ecb.control.bits.aesnk = op->cipher_key_size/4; /* AES key size */ 
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;        			
        	}
            break;
            
        case CIPHER_DEC:
        	switch(op->cipher_algorithm)
        	{
            	case CBC_DES:
            	    op->iv_size = 8;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size); /* set IV */
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;
#else
                    tdflag = 0x1b;
#endif                    
                    /* hardware decryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;
        			
                case ECB_DES:
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware decryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
                    break;
        			
                case CBC_3DES:
            	    op->iv_size = 8;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size); /* set IV */
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;
#else
                    tdflag = 0x1b;
#endif                    
                    /* hardware decryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
                    break;
        			
                case ECB_3DES:
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware decryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
           			break;
        			
                case CBC_AES:
                    cbc.control.bits.aesnk = op->cipher_key_size/4; /* AES key size */ 
            	    op->iv_size = 16;
                    ipsec_byte_change(op->iv,op->iv_size,iv,&iv_size);
                    memcpy(cbc.cipher_iv,iv,iv_size); /* set IV */
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x7b;
#else
                    tdflag = 0x1b;
#endif                    
                    /* hardware decryption */
                    ipsec_hw_cipher((unsigned char *)&cbc,sizeof(IPSEC_CIPHER_CBC_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;
        			
                case ECB_AES:
                    ecb.control.bits.aesnk = op->cipher_key_size/4; /* AES key size */ 
#ifdef CONFIG_IPSEC_GEMINI                    
                    tdflag = 0x73;
#else
                    tdflag = 0x13;
#endif                    
                    /* hardware encryption */
                    ipsec_hw_cipher((unsigned char *)&ecb,sizeof(IPSEC_CIPHER_ECB_T),
                                    op->in_packet,op->pkt_len,tdflag,
                                    ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);
        			break;        			
        	}
            break;
                        
        case AUTH:
            switch (op->auth_algorithm)
            {
                 case MD5:
                    if (op->auth_result_mode == APPEND_MODE)
                    {
                        ctrl_pkt_len = 8;
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x05; 
#else
                        tdflag = 0x05;
#endif                        
                    }    
                    else
                    {
                        ipsec_result.sw_pkt_len = 16;
                        ipsec_byte_change(ipsec_result.sw_cipher,ipsec_result.sw_pkt_len,auth_result,&auth_result_len);
                        memcpy(auth.var.auth_check_val,auth_result,auth_result_len);
                        ctrl_pkt_len = 28;
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x205;
#else
                        tdflag = 0x45;
#endif                        
                    }    
                        
                    auth.var.control.bits.auth_check_len = 4; /* 4-word to be checked or appended */
                    ipsec_hw_auth((unsigned char *)&auth,ctrl_pkt_len,
                                op->in_packet,op->pkt_len,tdflag,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);       
                    break;
                    
                case SHA1:    
                    if (op->auth_result_mode == APPEND_MODE)
                    {
                        ctrl_pkt_len = sizeof(IPSEC_AUTH_T) - 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x05;
#else
                        tdflag = 0x05;
#endif                   
                    }    
                    else
                    {
                        ipsec_result.sw_pkt_len = 20;
                        ipsec_byte_change(ipsec_result.sw_cipher,ipsec_result.sw_pkt_len,auth_result,&auth_result_len);
                        memcpy(auth.var.auth_check_val,auth_result,auth_result_len);
                        ctrl_pkt_len = sizeof(IPSEC_AUTH_T);
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x205;
#else
                        tdflag = 0x45;
#endif                        
                    }    
                    auth.var.control.bits.auth_check_len = 5; /* 6-word to be checked or appended */
                    ipsec_hw_auth((unsigned char *)&auth,ctrl_pkt_len,
                                op->in_packet,op->pkt_len,tdflag,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);       
                    break;
                    
                case HMAC_MD5:
                    if (op->auth_result_mode == APPEND_MODE)
                    {
                        ctrl_pkt_len = sizeof(IPSEC_HMAC_AUTH_T) - 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x185;  /* 1+4+80+100 */
#else
                        tdflag = 0x25;
#endif                        
                    }    
                    else
                    {
                        ipsec_result.sw_pkt_len = 16;
                        ipsec_byte_change(ipsec_result.sw_cipher,ipsec_result.sw_pkt_len,auth_result,&auth_result_len);
                        memcpy(auth_hmac.auth_check_val,auth_result,auth_result_len);
                        ctrl_pkt_len = sizeof(IPSEC_HMAC_AUTH_T);
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x385; /* 1+4+80+100+200 */
#else
                        tdflag = 0x65;
#endif                        
                    }    
                    auth_hmac.control.bits.auth_check_len = 4; /* 4-word to be checked or appended */
                    ipsec_hw_auth((unsigned char *)&auth_hmac,ctrl_pkt_len,
                                op->in_packet,op->pkt_len,tdflag,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);       
                    break;
                    
                case HMAC_SHA1:
                    if (op->auth_result_mode == APPEND_MODE)
                    {
                        ctrl_pkt_len = sizeof(IPSEC_HMAC_AUTH_T) - 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x185;  /* 1+4+80+100 */
#else
                        tdflag = 0x25;
#endif                        
                    }    
                    else
                    {
                        ipsec_result.sw_pkt_len = 20;
                        ipsec_byte_change(ipsec_result.sw_cipher,ipsec_result.sw_pkt_len,auth_result,&auth_result_len);
                        memcpy(auth_hmac.auth_check_val,auth_result,auth_result_len);
                        ctrl_pkt_len = sizeof(IPSEC_HMAC_AUTH_T);
#ifdef CONFIG_IPSEC_GEMINI                    
                        tdflag = 0x385; /* 1+4+80+100+200 */
#else
                        tdflag = 0x65;
#endif                        
                    }    
                    auth_hmac.control.bits.auth_check_len = 5; /* 6-word to be checked or appended */
                    ipsec_hw_auth((unsigned char *)&auth_hmac,ctrl_pkt_len,
                                op->in_packet,op->pkt_len,tdflag,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);       
                    break;
                     
                 case FCS:
#if 0                 
                   if (op->auth_result_mode == APPEND_MODE)
                    {
                        ctrl_pkt_len = 8;
                        tdflag = 0x05;
                    }    
                    else

                    {
                        ctrl_pkt_len = 28;
                        tdflag = 0x45;
                    }    
                        
                    ipsec_hw_fcs((unsigned char *)&auth,ctrl_pkt_len,
                                op->in_packet,op->pkt_len,tdflag,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len); 
#endif
                    ipsec_hw_fcs((unsigned char *)&fcs_auth,28,
                                op->in_packet,op->pkt_len,0x45,
                                ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len); 
                    break;    
                }    
            break;
        
        default:
            break;            
    } 
    return (0);
}


/*======================================================================================================*/
/*    Generate random packet and do software/hardware authentication/encryption/decryption   */ 
/*======================================================================================================*/
static int ipsec_auth_and_cipher(struct IPSEC_PACKET_S  *op)
{
    unsigned char           iv[16];
    unsigned int            iv_size;
	unsigned int		    tdflag=0;
	unsigned char           cipher_key[32];
	unsigned int            cipher_key_size;
	unsigned char           auth_key[64];
	unsigned int            auth_key_size;
	unsigned int            control_packet_len;
	unsigned char           auth_result[20];
	unsigned int            auth_result_len;
    
    /* CBC mode */
    if ((op->cipher_algorithm == CBC_DES) || (op->cipher_algorithm == CBC_3DES) || (op->cipher_algorithm == CBC_AES))
    {
        if ((op->auth_algorithm == MD5) || (op->auth_algorithm == SHA1))
        {
            /* Authentication and Cipher CBC mode */
            memset(&cbc_auth,0x00,sizeof(IPSEC_CBC_AUTH_T));
            cbc_auth.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            cbc_auth.control.bits.cipher_algorithm = op->cipher_algorithm; /* cipher algorithm */ 
            cbc_auth.control.bits.process_id = op->process_id;   /* set frame process id */
            cbc_auth.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            cbc_auth.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(cbc_auth.cipher_key,cipher_key,cipher_key_size);
            cbc_auth.control.bits.auth_algorithm = op->auth_algorithm; /* authentication algorithm */ 
            cbc_auth.control.bits.auth_mode = op->auth_result_mode; /* append/check mode */ 
            cbc_auth.auth.bits.auth_header_len = op->auth_header_len;  /* the header length to be skipped by the cipher */
            cbc_auth.auth.bits.auth_algorithm_len = op->auth_algorithm_len;   /* the length of message body to be encrypted */
        }
        else    /* HMAC */
        {
            /* Authentication HMAC mode and Cipher CBC mode */
            memset(&cbc_auth_hmac,0x00,sizeof(IPSEC_CBC_AUTH_HMAC_T));
            cbc_auth_hmac.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            cbc_auth_hmac.control.bits.cipher_algorithm = op->cipher_algorithm; /* cipher algorithm */ 
            cbc_auth_hmac.control.bits.process_id = op->process_id;   /* set frame process id */
            cbc_auth_hmac.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            cbc_auth_hmac.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(cbc_auth_hmac.cipher_key,cipher_key,cipher_key_size);
            cbc_auth_hmac.control.bits.auth_algorithm = op->auth_algorithm; /* authentication algorithm */ 
            cbc_auth_hmac.control.bits.auth_mode = op->auth_result_mode; /* append/check mode */ 
            cbc_auth_hmac.auth.bits.auth_header_len = op->auth_header_len;  /* the header length to be skipped by the cipher */
            cbc_auth_hmac.auth.bits.auth_algorithm_len = op->auth_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->auth_key,op->auth_key_size,auth_key,&auth_key_size);
            memcpy(cbc_auth_hmac.auth_key,auth_key,auth_key_size);
        }
    }
    else    /* ECB mode */
    {
        if ((op->auth_algorithm == MD5) || (op->auth_algorithm == SHA1))
        {
            /* Authentication and Cipher ECB mode */
            memset(&ecb_auth,0x00,sizeof(IPSEC_ECB_AUTH_T));
            ecb_auth.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            ecb_auth.control.bits.cipher_algorithm = op->cipher_algorithm; /* cipher algorithm */ 
            ecb_auth.control.bits.process_id = op->process_id;   /* set frame process id */
            ecb_auth.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            ecb_auth.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(ecb_auth.cipher_key,cipher_key,cipher_key_size);
            ecb_auth.control.bits.auth_algorithm = op->auth_algorithm; /* authentication algorithm */ 
            ecb_auth.control.bits.auth_mode = op->auth_result_mode; /* append/check mode */ 
            ecb_auth.auth.bits.auth_header_len = op->auth_header_len;  /* the header length to be skipped by the cipher */
            ecb_auth.auth.bits.auth_algorithm_len = op->auth_algorithm_len;   /* the length of message body to be encrypted */
        }
        else    /* HMAC */
        {
            /* Authentication HMAC mode and Cipher ECB mode */
            memset(&ecb_auth_hmac,0x00,sizeof(IPSEC_ECB_AUTH_HMAC_T));
            ecb_auth_hmac.control.bits.op_mode = op->op_mode;    /* cipher encryption */
            ecb_auth_hmac.control.bits.cipher_algorithm = op->cipher_algorithm; /* cipher algorithm */ 
            ecb_auth_hmac.control.bits.process_id = op->process_id;   /* set frame process id */
            ecb_auth_hmac.cipher.bits.cipher_header_len = op->cipher_header_len;  /* the header length to be skipped by the cipher */
            ecb_auth_hmac.cipher.bits.cipher_algorithm_len = op->cipher_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->cipher_key,op->cipher_key_size,cipher_key,&cipher_key_size);
            memcpy(ecb_auth_hmac.cipher_key,cipher_key,cipher_key_size);
            ecb_auth_hmac.control.bits.auth_algorithm = op->auth_algorithm; /* authentication algorithm */ 
            ecb_auth_hmac.control.bits.auth_mode = op->auth_result_mode; /* append/check mode */ 
            ecb_auth_hmac.auth.bits.auth_header_len = op->auth_header_len;  /* the header length to be skipped by the cipher */
            ecb_auth_hmac.auth.bits.auth_algorithm_len = op->auth_algorithm_len;   /* the length of message body to be encrypted */
            ipsec_byte_change(op->auth_key,op->auth_key_size,auth_key,&auth_key_size);
            memcpy(ecb_auth_hmac.auth_key,auth_key,auth_key_size);
        }
    }    
    
    switch (op->op_mode)
    {
        case ENC_AUTH:
        	switch(op->cipher_algorithm)
        	{
            	case CBC_DES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60;
#ifdef CONFIG_IPSEC_GEMINI                                                
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                    }
        			break;
        			
                case ECB_DES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                             break;
                    }
                    break;
        			
                case CBC_3DES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40*/
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                             break;
                    }
                    break;
        			
                case ECB_3DES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40*/
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100*/
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                             break;
                    }
           			break;
        			
                case CBC_AES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                    }
        			break;
        			
                case ECB_AES:
                    switch (op->auth_algorithm)
                    {
                        case MD5:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case SHA1:    
                            //control_packet_len = 4 + 4 + 4 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_MD5:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                            
                        case HMAC_SHA1:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                             break;
                    }
        			break;        			
        	}            
            break;
            
        case AUTH_DEC:
            switch (op->auth_algorithm)
            {
                case MD5:
                	switch(op->cipher_algorithm)
                	{
                    	case CBC_DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;
                			
                        case ECB_DES:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case CBC_3DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case ECB_3DES:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                   			break;
                			
                        case CBC_AES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
               			    break;
                			
                        case ECB_AES:
                            //control_packet_len = 4 + 4 + 4 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth.control.bits.auth_check_len = 4;
                             if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                           
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;        			
                	}
                    break;
                    
                case SHA1:    
                	switch(op->cipher_algorithm)
                	{
                    	case CBC_DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;
                			
                        case ECB_DES:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case CBC_3DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = 0;
                            cbc_auth.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case ECB_3DES:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = 0;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                   			break;
                			
                        case CBC_AES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 ;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10;
                            control_packet_len = 60 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x7f;  /* 1+2+4+8+10+20+40 */
#else
                            tdflag = 0x1f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
               			break;
                			
                        case ECB_AES:
                            //control_packet_len = 4 + 4 + 4 + 32;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10;
                            control_packet_len = 44 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x77;  /* 1+2+4+10+20+40 */
#else
                            tdflag = 0x17;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;        			
                	}
                    break;
                    
                case HMAC_MD5:
                	switch(op->cipher_algorithm)
                	{
                    	case CBC_DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;
                			
                        case ECB_DES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case CBC_3DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;  /* 1+2+4+8+10+20+40+80+100 */
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case ECB_3DES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;  /* 1+2+4+10+20+40+80+100 */
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                   			break;
                			
                        case CBC_AES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth_hmac.control.bits.auth_check_len = 4;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
               			    break;
                			
                        case ECB_AES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth_hmac.control.bits.auth_check_len = 4;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],16,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;        			
                	}
                    break;
                    
                case HMAC_SHA1:
                	switch(op->cipher_algorithm)
                	{
                    	case CBC_DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;
                			
                        case ECB_DES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case CBC_3DES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = 0;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,8,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                            break;
                			
                        case ECB_3DES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = 0;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                   			break;
                			
                        case CBC_AES:
                            //control_packet_len = 4 + 4 + 4 + 16 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x08 + 0x10 + 0x20;
                            control_packet_len = 124 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1ff;
#else
                            tdflag = 0x3f;
#endif                            
                            /* IPSec Control Register */
                            cbc_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            cbc_auth_hmac.control.bits.auth_check_len = 5;
                            /* Cipher IV */
                            ipsec_byte_change(op->iv,16,iv,&iv_size);
                            memcpy(cbc_auth_hmac.cipher_iv,iv,iv_size);
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(cbc_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&cbc_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
               			    break;
                			
                        case ECB_AES:
                            //control_packet_len = 4 + 4 + 4 + 32 + 64;
                            //tdflag = 0x01 + 0x02 + 0x04 + 0x10 + 0x20;
                            control_packet_len = 108 ;
#ifdef CONFIG_IPSEC_GEMINI                    
                            tdflag = 0x1f7;
#else
                            tdflag = 0x37;
#endif                            
                            /* IPSec Control Register */
                            ecb_auth_hmac.control.bits.aesnk = op->cipher_key_size/4;
                            ecb_auth_hmac.control.bits.auth_check_len = 5;
                            if (op->auth_result_mode == CHECK_MODE)
                            {
                                ipsec_byte_change(&ipsec_result.sw_cipher[op->pkt_len],20,auth_result,&auth_result_len);
                                memcpy(ecb_auth_hmac.auth_check_val,auth_result,auth_result_len);
                                control_packet_len = control_packet_len + 20;
#ifdef CONFIG_IPSEC_GEMINI                    
                                tdflag = tdflag + 0x200;
#else
                                tdflag = tdflag + 0x40;
#endif                                
                            }
                            
                            ipsec_hw_cipher((unsigned char *)&ecb_auth_hmac,control_packet_len,
                                        op->in_packet,op->pkt_len,tdflag,
                                        ipsec_result.hw_cipher,&ipsec_result.hw_pkt_len);                                    
                			break;        			
                	}
                    break;
            }
            break;
                        
    }
    return (0);
}

   


/*======================================================================================================*/
/*    Hardware authentication & encrypt & decrypt function    */ 
/*======================================================================================================*/
//void ipsec_hw_cipher(unsigned char *ctrl_pkt,int ctrl_len,unsigned char *data_pkt, int data_len, unsigned int tqflag,
//                           unsigned char *out_pkt,int *out_len)
void ipsec_hw_cipher(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
													unsigned char *out_pkt,int *out_len)
{
    unsigned int        ipsec_status; 
    unsigned int        i;  
		struct scatterlist sg[1];
		
	//disable_irq(IRQ_IPSEC);
		sg[0].page = virt_to_page(ctrl_pkt);
		sg[0].offset = offset_in_page(ctrl_pkt);
		sg[0].length = ctrl_len;
		
    //ipsec_tx_packet(ctrl_pkt,ctrl_len,tqflag);
    ipsec_tx_packet(sg,ctrl_len,tqflag);
    //if(ipsec_interrupt_polling()==0)
    //	{
    //		//printk("ok\n");
    //	}
    //else
    //		printk("%s : polling 1\n",__func__);
#if 1    
    for (i=0;i<5000;i++)
    {
        ipsec_status = ipsec_read_reg(IPSEC_STATUS_REG);
        if ((ipsec_status & 0x00000fff)==0) /* check IPSec status register */
        {
            break;
        }    
    }    
    if ((ipsec_status & 0x00000fff) != 0)
    {
        printk("\nipsec_hw_cipher : IPSEC Control Packet Error !!!(%08x)\n",ipsec_status); 
    }
#endif    
	//enable_irq(IRQ_IPSEC);
    ipsec_tx_packet(data_pkt,data_len,0);

   
    if(ipsec_interrupt_polling()==0)
    	{
    		//printk("ok\n");
    	}
    	else
    		printk("%s : polling\n",__func__);

    
    
}    

void ipsec_hw_auth(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
                            unsigned char *out_pkt,int *out_len)
{
    unsigned int        ipsec_status;
    unsigned int        i;   
		struct scatterlist sg[1];
		
	//disable_irq(IRQ_IPSEC);
    sg[0].page = virt_to_page(ctrl_pkt);
		sg[0].offset = offset_in_page(ctrl_pkt);
		sg[0].length = ctrl_len;
		
    //ipsec_tx_packet(ctrl_pkt,ctrl_len,tqflag);
    ipsec_tx_packet(sg,ctrl_len,tqflag);
    //if(ipsec_interrupt_polling()==0)
    //	{
    //		//printk("ok\n");
    //	}
    //	else
    //		printk("%s : polling  1\n",__func__);
#if 1    
    for (i=0;i<5000;i++)
    {
        ipsec_status = ipsec_read_reg(IPSEC_STATUS_REG);
        if ((ipsec_status & 0x00000fff)==0) /* check IPSec status register */
        {
            break;
        }    
    }    
    if ((ipsec_status & 0x00000fff) != 0)
    {
        printk("\nipsec_hw_auth : IPSEC Control Packet Error !!!(%08x)\n",ipsec_status); 
    }
#endif    
	//enable_irq(IRQ_IPSEC);
    ipsec_tx_packet(data_pkt,data_len,0);

	if(ipsec_interrupt_polling()==0)
    	{
    		//printk("ok\n");
    	}
    	else
    		printk("%s : polling\n",__func__);
}    

void ipsec_hw_fcs(unsigned char *ctrl_pkt,int ctrl_len,struct scatterlist *data_pkt, int data_len, unsigned int tqflag,
                            unsigned char *out_pkt,int *out_len)
{
	IPSEC_DMA_STATUS_T	status;
    unsigned int        ipsec_status;
    unsigned int        reg_val;
    unsigned int        i;   
		struct scatterlist sg[1];
	//disable_irq(IRQ_IPSEC);

    sg[0].page = virt_to_page(ctrl_pkt);
		sg[0].offset = offset_in_page(ctrl_pkt);
		sg[0].length = ctrl_len;
		
    //ipsec_tx_packet(ctrl_pkt,ctrl_len,tqflag);
    ipsec_tx_packet(sg,ctrl_len,tqflag);

#if 1
    for (i=0;i<=100;i++)
    {
        ipsec_status = ipsec_read_reg(IPSEC_STATUS_REG);
        if ((ipsec_status & 0x00000fff)==0) /* check IPSec status register */
        {
            break;
        }    
        if ( i == 100)
        {
            printk("\nipsec_hw_fcs : IPSEC Control Packet Error !!!(%08x)\n",ipsec_status); 
        }
    }
#endif        
//    ipsec_write_reg(IPSEC_DMA_STATUS,status.bits32,0xffffffff); /* clear IPSec DMA status register */
    ipsec_tx_packet(data_pkt,data_len,0);

    if (ipsec_interrupt_polling() == 0)
    {
        
    }    
}    

static int ipsec_buf_init(void)
{
	dma_addr_t   tx_first_desc_dma=0;
	dma_addr_t   rx_first_desc_dma=0;
//	dma_addr_t   tx_first_buf_dma=0;
//	dma_addr_t   rx_first_buf_dma=0;
	int    i;

    tp = kmalloc(sizeof(IPSEC_T),GFP_ATOMIC);
    if (tp == NULL)
    {
        printk("memory allocation fail !\n");
    }    

#if (ZERO_COPY == 0)
	/* allocates TX/RX DMA packet buffer */
	/* tx_buf_virtual:virtual address  tp.tx_bufs_dma:physical address */
	tp->tx_bufs = DMA_MALLOC(TX_BUF_TOT_LEN,(dma_addr_t *)&tp->tx_bufs_dma);
    tx_buf_virtual_base = (unsigned int)tp->tx_bufs - (unsigned int)tp->tx_bufs_dma;
	memset(tp->tx_bufs,0x00,TX_BUF_TOT_LEN);
	tx_first_buf_dma = tp->tx_bufs_dma;     /* physical address of tx buffer */
	tp->rx_bufs = DMA_MALLOC(RX_BUF_TOT_LEN,(dma_addr_t *)&tp->rx_bufs_dma);
    rx_buf_virtual_base = (unsigned int)tp->rx_bufs - (unsigned int)tp->rx_bufs_dma;
	memset(tp->rx_bufs,0x00,RX_BUF_TOT_LEN);
	rx_first_buf_dma = tp->rx_bufs_dma;     /* physical address of rx buffer */
    printk("ipsec tx_buf = %08x\n",(unsigned int)tp->tx_bufs);
    printk("ipsec rx_buf = %08x\n",(unsigned int)tp->rx_bufs);
    printk("ipsec tx_buf_dma = %08x\n",tp->tx_bufs_dma);
    printk("ipsec rx_buf_dma = %08x\n",tp->rx_bufs_dma);
#endif
	
	/* allocates TX/RX descriptors */
	tp->tx_desc = DMA_MALLOC(TX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(dma_addr_t *)&tp->tx_desc_dma);
    tx_desc_virtual_base = (unsigned int)tp->tx_desc - (unsigned int)tp->tx_desc_dma;
    memset(tp->tx_desc,0x00,TX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T));
	tp->rx_desc = DMA_MALLOC(RX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(dma_addr_t *)&tp->rx_desc_dma);
    rx_desc_virtual_base = (unsigned int)tp->rx_desc - (unsigned int)tp->rx_desc_dma;
    memset(tp->rx_desc,0x00,RX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T));
    
#if (ZERO_COPY == 0)
	if (tp->tx_bufs==0x00 || tp->rx_bufs==0x00 || tp->tx_desc==0x00 || tp->rx_desc==0x00) 
#else
	if (tp->tx_desc==0x00 || tp->rx_desc==0x00) 
#endif
	{
#if (ZERO_COPY == 0)
		if (tp->tx_bufs)
			DMA_MFREE(tp->tx_bufs, TX_BUF_TOT_LEN, (unsigned int)tp->tx_bufs_dma);
		if (tp->rx_bufs)
			DMA_MFREE(tp->rx_bufs, RX_BUF_TOT_LEN, (unsigned int)tp->rx_bufs_dma);
#endif
		if (tp->tx_desc)
			DMA_MFREE(tp->tx_desc, TX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(unsigned int)tp->tx_desc_dma);
		if (tp->rx_desc)
			DMA_MFREE(tp->rx_desc, RX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(unsigned int)tp->rx_desc_dma);
		return -ENOMEM;
	}
	
	/* TX descriptors initial */
	tp->tx_cur_desc = tp->tx_desc;          /* virtual address */
	tp->tx_finished_desc = tp->tx_desc;     /* virtual address */
	tx_first_desc_dma = tp->tx_desc_dma;    /* physical address */
	for (i = 1; i < TX_DESC_NUM; i++)
	{
		tp->tx_desc->frame_ctrl.bits.own = CPU; /* set owner to CPU */
		tp->tx_desc->frame_ctrl.bits.buffer_size = TX_BUF_SIZE;  /* set tx buffer size for descriptor */
#if (ZERO_COPY == 0)
		tp->tx_desc->buf_adr = tp->tx_bufs_dma; /* set data buffer address */
		tp->tx_bufs_dma = tp->tx_bufs_dma + TX_BUF_SIZE; /* point to next buffer address */
#endif
		tp->tx_desc_dma = tp->tx_desc_dma + sizeof(IPSEC_DESCRIPTOR_T); /* next tx descriptor DMA address */
		tp->tx_desc->next_desc.next_descriptor = tp->tx_desc_dma | 0x0000000b;
		tp->tx_desc = &tp->tx_desc[1] ; /* next tx descriptor virtual address */
	}
	/* the last descriptor will point back to first descriptor */
	tp->tx_desc->frame_ctrl.bits.own = CPU;
	tp->tx_desc->frame_ctrl.bits.buffer_size = TX_BUF_SIZE;
#if (ZERO_COPY == 0)
	tp->tx_desc->buf_adr = (unsigned int)tp->tx_bufs_dma;
#endif
	tp->tx_desc->next_desc.next_descriptor = tx_first_desc_dma | 0x0000000b;
	tp->tx_desc = tp->tx_cur_desc;
	tp->tx_desc_dma = tx_first_desc_dma;
#if (ZERO_COPY == 0)
	tp->tx_bufs_dma = tx_first_buf_dma;
#endif
	
	/* RX descriptors initial */
	tp->rx_cur_desc = tp->rx_desc;
	rx_first_desc_dma = tp->rx_desc_dma;
	rx_desc_index[0] = tp->rx_desc;
	for (i = 1; i < RX_DESC_NUM; i++)
	{
		tp->rx_desc->frame_ctrl.bits.own = DMA;  /* set owner bit to DMA */
		tp->rx_desc->frame_ctrl.bits.buffer_size = RX_BUF_SIZE; /* set rx buffer size for descriptor */
#if (ZERO_COPY == 0)
		tp->rx_desc->buf_adr = tp->rx_bufs_dma;   /* set data buffer address */
		tp->rx_bufs_dma = tp->rx_bufs_dma + RX_BUF_SIZE;    /* point to next buffer address */
#endif
		tp->rx_desc_dma = tp->rx_desc_dma + sizeof(IPSEC_DESCRIPTOR_T); /* next rx descriptor DMA address */
		tp->rx_desc->next_desc.next_descriptor = tp->rx_desc_dma | 0x0000000b;
		tp->rx_desc = &tp->rx_desc[1]; /* next rx descriptor virtual address */
	    rx_desc_index[i] = tp->rx_desc;
	}
	/* the last descriptor will point back to first descriptor */
	tp->rx_desc->frame_ctrl.bits.own = DMA;
	tp->rx_desc->frame_ctrl.bits.buffer_size = RX_BUF_SIZE;
#if (ZERO_COPY == 0)
	tp->rx_desc->buf_adr = tp->rx_bufs_dma;
#endif
	tp->rx_desc->next_desc.next_descriptor = rx_first_desc_dma | 0x0000000b;
	tp->rx_desc = tp->rx_cur_desc;
	tp->rx_desc_dma = rx_first_desc_dma;
#if (ZERO_COPY == 0)
	tp->rx_bufs_dma = rx_first_buf_dma;
#endif	
    printk("ipsec tx_desc = %08x\n",(unsigned int)tp->tx_desc);
    printk("ipsec rx_desc = %08x\n",(unsigned int)tp->rx_desc);
    printk("ipsec tx_desc_dma = %08x\n",tp->tx_desc_dma);
    printk("ipsec rx_desc_dma = %08x\n",tp->rx_desc_dma);
	return (0);	
}

static void ipsec_hw_start(void)
{
	IPSEC_TXDMA_CURR_DESC_T	tx_desc;
	IPSEC_RXDMA_CURR_DESC_T	rx_desc;
    IPSEC_TXDMA_CTRL_T       txdma_ctrl,txdma_ctrl_mask;
    IPSEC_RXDMA_CTRL_T       rxdma_ctrl,rxdma_ctrl_mask;
	IPSEC_DMA_STATUS_T       dma_status,dma_status_mask;

    ipsec_sw_reset();
	
	ipsec_write_reg(0xff40,0x00000044,0xffffffff);

	/* program TxDMA Current Descriptor Address register for first descriptor */
	tx_desc.bits32 = (unsigned int)(tp->tx_desc_dma);
	tx_desc.bits.eofie = 1;
	tx_desc.bits.dec = 0;
	tx_desc.bits.sof_eof = 0x03;
	ipsec_write_reg(IPSEC_TXDMA_CURR_DESC,tx_desc.bits32,0xffffffff);
	
	/* program RxDMA Current Descriptor Address register for first descriptor */
	rx_desc.bits32 = (unsigned int)(tp->rx_desc_dma);
	rx_desc.bits.eofie = 1;
	rx_desc.bits.dec = 0;
	rx_desc.bits.sof_eof = 0x03;
	ipsec_write_reg(IPSEC_RXDMA_CURR_DESC,rx_desc.bits32,0xffffffff);
		
	/* enable IPSEC interrupt & disable loopback */
	dma_status.bits32 = 0;
	dma_status.bits.loop_back = 0;
	dma_status_mask.bits32 = 0;
	dma_status_mask.bits.loop_back = 1;
	ipsec_write_reg(IPSEC_DMA_STATUS,dma_status.bits32,dma_status_mask.bits32);
	
	txdma_ctrl.bits32 = 0;
	txdma_ctrl.bits.td_start = 0;    /* start DMA transfer */
	txdma_ctrl.bits.td_continue = 0; /* continue DMA operation */
	txdma_ctrl.bits.td_chain_mode = 1; /* chain mode */
	txdma_ctrl.bits.td_prot = 0;
	txdma_ctrl.bits.td_burst_size = 1;
	txdma_ctrl.bits.td_bus = 0;
	txdma_ctrl.bits.td_endian = 0;
	txdma_ctrl.bits.td_finish_en = 1;
	txdma_ctrl.bits.td_fail_en = 1;
	txdma_ctrl.bits.td_perr_en = 1;
	txdma_ctrl.bits.td_eod_en = 1;
	txdma_ctrl.bits.td_eof_en = 1;
	txdma_ctrl_mask.bits32 = 0;
	txdma_ctrl_mask.bits.td_start = 1;    
	txdma_ctrl_mask.bits.td_continue = 1; 
	txdma_ctrl_mask.bits.td_chain_mode = 1;
	txdma_ctrl_mask.bits.td_prot = 0xf;
	txdma_ctrl_mask.bits.td_burst_size = 3;
	txdma_ctrl_mask.bits.td_bus = 3;
	txdma_ctrl_mask.bits.td_endian = 1;
	txdma_ctrl_mask.bits.td_finish_en = 1;
	txdma_ctrl_mask.bits.td_fail_en = 1;
	txdma_ctrl_mask.bits.td_perr_en = 1;
	txdma_ctrl_mask.bits.td_eod_en = 1;
	txdma_ctrl_mask.bits.td_eof_en = 1;
	ipsec_write_reg(IPSEC_TXDMA_CTRL,txdma_ctrl.bits32,txdma_ctrl_mask.bits32);

	rxdma_ctrl.bits32 = 0;
	rxdma_ctrl.bits.rd_start = 0;    /* start DMA transfer */
	rxdma_ctrl.bits.rd_continue = 0; /* continue DMA operation */
	rxdma_ctrl.bits.rd_chain_mode = 1;   /* chain mode */
	rxdma_ctrl.bits.rd_prot = 0;
	rxdma_ctrl.bits.rd_burst_size = 1;
	rxdma_ctrl.bits.rd_bus = 0;
	rxdma_ctrl.bits.rd_endian = 0;
	rxdma_ctrl.bits.rd_finish_en = 1;
	rxdma_ctrl.bits.rd_fail_en = 1;
	rxdma_ctrl.bits.rd_perr_en = 1;
	rxdma_ctrl.bits.rd_eod_en = 1;
	rxdma_ctrl.bits.rd_eof_en = 1;
	rxdma_ctrl_mask.bits32 = 0;
	rxdma_ctrl_mask.bits.rd_start = 1;    
	rxdma_ctrl_mask.bits.rd_continue = 1; 
	rxdma_ctrl_mask.bits.rd_chain_mode = 1;
	rxdma_ctrl_mask.bits.rd_prot = 15;
	rxdma_ctrl_mask.bits.rd_burst_size = 3;
	rxdma_ctrl_mask.bits.rd_bus = 3;
	rxdma_ctrl_mask.bits.rd_endian = 1;
	rxdma_ctrl_mask.bits.rd_finish_en = 1;
	rxdma_ctrl_mask.bits.rd_fail_en = 1;
	rxdma_ctrl_mask.bits.rd_perr_en = 1;
	rxdma_ctrl_mask.bits.rd_eod_en = 1;
	rxdma_ctrl_mask.bits.rd_eof_en = 1;
	ipsec_write_reg(IPSEC_RXDMA_CTRL,rxdma_ctrl.bits32,rxdma_ctrl_mask.bits32);
	
    return;	
}	

static void ipsec_complete_tx_packet(void)
{
    IPSEC_DESCRIPTOR_T	    *tx_complete_desc;
    IPSEC_DESCRIPTOR_T	    *tx_finished_desc = tp->tx_finished_desc;
    unsigned int desc_cnt;
    unsigned int i;
    
	tx_complete_desc = (IPSEC_DESCRIPTOR_T *)((ipsec_read_reg(IPSEC_TXDMA_CURR_DESC) & 0xfffffff0)+tx_desc_virtual_base);
	
	/* check tx status and accumulate tx statistics */
    for (;;)
    {
    	if (tx_finished_desc->frame_ctrl.bits.own == CPU)
    	{
    	    if ( (tx_finished_desc->frame_ctrl.bits.derr) ||
    	         (tx_finished_desc->frame_ctrl.bits.perr) )
    	    {
    	        printk("Descriptor Processing Error !!!\n");    	        
    	    }
    	              
            desc_cnt = tx_finished_desc->frame_ctrl.bits.desc_count;

        	for (i=1; i<desc_cnt; i++)  /* multi_descriptor */
        	{
                tx_finished_desc = (IPSEC_DESCRIPTOR_T *)((tx_finished_desc->next_desc.next_descriptor & 0xfffffff0)+tx_desc_virtual_base);
                tx_finished_desc->frame_ctrl.bits.own = CPU;
         	}
            tx_finished_desc = (IPSEC_DESCRIPTOR_T *)((tx_finished_desc->next_desc.next_descriptor & 0xfffffff0)+tx_desc_virtual_base);
            if (tx_finished_desc == tx_complete_desc)
            {
                break;
            }    
        }
     	else
     	{
     	    break;
     	}        
    }	
    tp->tx_finished_desc = tx_finished_desc;
}	

//static int ipsec_tx_packet(unsigned char *packet, int len, unsigned int tqflag)
static int ipsec_tx_packet(struct scatterlist *packet, int len, unsigned int tqflag)
{
    IPSEC_DESCRIPTOR_T	        *tx_desc = tp->tx_cur_desc;
//	IPSEC_TXDMA_CTRL_T		    tx_ctrl,tx_ctrl_mask;
//	IPSEC_RXDMA_CTRL_T		    rx_ctrl,rx_ctrl_mask;
	IPSEC_TXDMA_FIRST_DESC_T	txdma_busy;
	unsigned int                desc_cnt;
	unsigned int                i,tmp_len;
	unsigned int                sof;
	unsigned int                last_desc_byte_cnt;
	unsigned char               *pkt_ptr;
	unsigned int                reg_val;

	if (tx_desc->frame_ctrl.bits.own != CPU)
	{
	    printk("\nipsec_tx_packet : Tx Descriptor Error !\n");
        ipsec_read_reg(0x0000);
    }
//#if (ZERO_COPY == 0)
//    pkt_ptr = packet;
//#else
//    pkt_ptr = kmap(packet[0].page) + packet[0].offset;
//		//consistent_sync(pkt_ptr,packet[0].length,PCI_DMA_TODEVICE);
//    pkt_ptr = (unsigned char *)virt_to_phys(pkt_ptr);  //__pa(packet);   
//	ipsec_write_reg2(IPSEC_TXDMA_BUF_ADDR,(unsigned int)pkt_ptr);
////	
////    consistent_sync(packet,len,PCI_DMA_TODEVICE);
////    pkt_ptr = (unsigned char *)virt_to_phys(packet);  //__pa(packet);
//////	ipsec_write_reg(IPSEC_TXDMA_BUF_ADDR,(unsigned int)pkt_ptr,0xffffffff);    
////	ipsec_write_reg2(IPSEC_TXDMA_BUF_ADDR,(unsigned int)pkt_ptr);    
//#endif    
	sof = 0x02; /* the first descriptor */
	desc_cnt = (len/TX_BUF_SIZE) ;
    last_desc_byte_cnt = len % TX_BUF_SIZE;
	//for (i=0; i<desc_cnt ;i++)
	tmp_len=0;i=0;
	while(tmp_len < len)
	{
	    tx_desc->frame_ctrl.bits32 = 0;
	    tx_desc->flag_status.bits32 = 0;
	   
		tx_desc->frame_ctrl.bits.buffer_size = packet[i].length; /* descriptor byte count */
        tx_desc->flag_status.bits_tx_flag.tqflag = tqflag;    /* set tqflag */

		pkt_ptr = kmap(packet[i].page) + packet[i].offset;
    consistent_sync(pkt_ptr,packet[i].length,PCI_DMA_TODEVICE);
    pkt_ptr = (unsigned char *)virt_to_phys(pkt_ptr);  //__pa(packet);  
    
    
#if (ZERO_COPY == 0)
		memcpy((char *)(tx_desc->buf_adr+tx_buf_virtual_base),pkt_ptr,packet[i].length); /* copy packet to descriptor buffer address */
        //pkt_ptr = &pkt_ptr[packet[i].length];
#else
        tx_desc->buf_adr = (unsigned int)pkt_ptr;
        //pkt_ptr = (unsigned char *)((unsigned int)pkt_ptr + packet[i].length);
#endif
        
        if ( (packet[i].length == len) && i==0 )
        {
            sof = 0x03; /*only one descriptor*/
        }    
		else if ( ((packet[i].length + tmp_len)== len) && (i != 0) )
		{
		    sof = 0x01; /*the last descriptor*/
		}    
		tx_desc->next_desc.bits.eofie = 1;
		tx_desc->next_desc.bits.dec = 0;
		tx_desc->next_desc.bits.sof_eof = sof;
		if (sof==0x02)  
		{
		    sof = 0x00; /* the linking descriptor */
		}

		wmb();

			
			///middle
			
        tmp_len+=packet[i].length;
        i++;
    	/* set owner bit */
	    tx_desc->frame_ctrl.bits.own = DMA;
	    	 
        tx_desc = (IPSEC_DESCRIPTOR_T *)((tx_desc->next_desc.next_descriptor & 0xfffffff0)+tx_desc_virtual_base);
	    if (tx_desc->frame_ctrl.bits.own != CPU)
	    {
	        printk("\nipsec_tx_packet : Tx Descriptor Error !\n");
        } 
        
        
    };        

    tp->tx_cur_desc = tx_desc;

//    consistent_sync(tx_desc,sizeof(IPSEC_DESCRIPTOR_T),DMA_BIDIRECTIONAL);

    /* if TX DMA process is stop->ed , restart it */    
   // if(tqflag>0)
   // 	return (0);
	txdma_busy.bits32 = ipsec_read_reg(IPSEC_TXDMA_FIRST_DESC);
	if (txdma_busy.bits.td_busy == 0)
	{
		/* restart Rx DMA process */
		reg_val = ipsec_read_reg(IPSEC_RXDMA_CTRL);
		reg_val |= (0x03<<30);
		ipsec_write_reg2(IPSEC_RXDMA_CTRL, reg_val);

		/* restart Tx DMA process */
		reg_val = ipsec_read_reg(IPSEC_TXDMA_CTRL);
		reg_val |= (0x03<<30);
		ipsec_write_reg2(IPSEC_TXDMA_CTRL, reg_val);
	}
	
    return (0);
}

static void ipsec_rx_packet(void)
{
    IPSEC_DESCRIPTOR_T      *rx_desc = tp->rx_cur_desc ;
    struct IPSEC_PACKET_S   *op_info ;
//    unsigned char           *pkt_ptr,*rx_buf_adr;
	unsigned int 		    pkt_len;
//	unsigned int            remain_pkt_len;
	unsigned int            desc_count;
	unsigned int            process_id;
	unsigned int            auth_cmp_result;
	unsigned int            checksum = 0;
//	unsigned int            own; 
	unsigned int            i;

    for (;;)
    {
        if (rx_desc->frame_ctrl.bits.own == CPU)
    	{
    	    if ( (rx_desc->frame_ctrl.bits.derr==1)||(rx_desc->frame_ctrl.bits.perr==1) )
    	    {
    	        printk("ipsec_rx_packet : Descriptor Processing Error !!!\n");
    	    }
    	    pkt_len = rx_desc->flag_status.bits_rx_status.frame_count;  /* total byte count in a frame*/
            process_id = rx_desc->flag_status.bits_rx_status.process_id; /* get process ID from descriptor */
            auth_cmp_result = rx_desc->flag_status.bits_rx_status.auth_result;
            wep_crc_ok = rx_desc->flag_status.bits_rx_status.wep_crc_ok;
            tkip_mic_ok = rx_desc->flag_status.bits_rx_status.tkip_mic_ok;
            ccmp_mic_ok = rx_desc->flag_status.bits_rx_status.ccmp_mic_ok;
    	    desc_count = rx_desc->frame_ctrl.bits.desc_count; /* get descriptor count per frame */ 
//            checksum = rx_desc->flag_status.bits_rx_status.checksum ;
//            checksum = checksum + rx_desc->frame_ctrl.bits.checksum * 256;
    	}
    	else
    	{
    	    return;
    	}    

        /* get request information from queue */
        if ((op_info = ipsec_get_queue(ipsec_queue))!=NULL)
        {
//    printk("%s : ipsec_get_queue op_info->process_id=%d pkt_len=%d\n",__func__,op_info->process_id,op_info->pkt_len);
            /* fill request result */
    				consistent_sync(op_info->out_packet,pkt_len,DMA_BIDIRECTIONAL);
            op_info->out_pkt_len = pkt_len;
            op_info->auth_cmp_result = auth_cmp_result;
            op_info->checksum = checksum;
            op_info->status = 0;
    		
    		//if(op_info->auth_result_mode)
    		//	op_info->out_pkt_len-=0x10;
            //if ((op_info->out_pkt_len != op_info->pkt_len) || (op_info->process_id != process_id))
            if ((op_info->process_id != process_id))
            {
                op_info->status = 2;
                printk(" op_info->out_pkt_len =%x , op_info->pkt_len= %x\n",op_info->out_pkt_len,op_info->pkt_len);
                printk("ipsec_rx_packet:Process ID or Packet Length Error %d %d !\n",op_info->process_id,process_id);
            }

        }    
        else
        {
            op_info->status = 1;
            printk("ipsec_rx_packet:IPSec Queue Empty!\n");
        }    
    
#if (ZERO_COPY == 0)	    
        if (op_info > 0)
        {
            pkt_ptr = &op_info->out_packet[0];
        }
        
    	remain_pkt_len = pkt_len;
#endif
	
    	for (i=0; i<desc_count; i++)
    	{
#if (ZERO_COPY == 0)	    
    	    if (op_info > 0)
    	    {    
        	    rx_buf_adr = (char *)(rx_desc->buf_adr+rx_buf_virtual_base);
        	    if ( remain_pkt_len < RX_BUF_SIZE )
        	    {
                    memcpy(pkt_ptr,rx_buf_adr,remain_pkt_len);  
                    //hw_memcpy(pkt_ptr,rx_buf_adr,remain_pkt_len);  
                }
                else
                {
                    memcpy(pkt_ptr,rx_buf_adr,RX_BUF_SIZE);
                    //hw_memcpy(pkt_ptr,rx_buf_adr,RX_BUF_SIZE);
                    pkt_ptr = &pkt_ptr[RX_BUF_SIZE];
                    remain_pkt_len = remain_pkt_len - RX_BUF_SIZE;
                }
            }
#endif        
            /* return RX descriptor to DMA */
    	    rx_desc->frame_ctrl.bits.own = DMA;
            /* get next RX descriptor pointer */
            rx_desc = (IPSEC_DESCRIPTOR_T *)((rx_desc->next_desc.next_descriptor & 0xfffffff0)+rx_desc_virtual_base);
        }
        tp->rx_cur_desc = rx_desc;
//        wake_up_interruptible(&ipsec_wait_q);
    
        /* to call callback function */
        //if (op_info > 0)
        //{
        //    if (op_info->callback)
        //    {
        //        op_info->callback(op_info);
        //    }    
        //}
    }           
}

static irqreturn_t ipsec_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	IPSEC_DMA_STATUS_T	status;
	int                 handled = 0;
    		
	handled = 1;

	//disable_irq(IRQ_IPSEC);
    for (;;)
    {
        /* read DMA status */
	    status.bits32 = ipsec_read_reg(IPSEC_DMA_STATUS);

	    /* clear DMA status */
        ipsec_write_reg(IPSEC_DMA_STATUS,status.bits32,status.bits32);	
        
        if ((status.bits32 & 0xffffc000)==0)
        {
            break;
        }  
            
        if ((status.bits32 & 0x63000000) > 0)
        { 
            printk("Error :");       
        	if (status.bits.ts_derr==1)
        	{
        	    printk("AHB bus Error While Tx !!!\n");
                
        	}
        	if (status.bits.ts_perr==1)
        	{
        	    printk("Tx Descriptor Protocol Error !!!\n");
                
        	}    
        	if (status.bits.rs_derr==1)
        	{
        	    printk("AHB bus Error While Rx !!!\n");
                
        	}
        	if (status.bits.rs_perr==1)
        	{
        	    printk("Rx Descriptor Protocol Error !!!\n");         
        	} 
        }
        	   	    
        if (status.bits.ts_eofi==1)
        {
            ipsec_complete_tx_packet();
        }    
        if (status.bits.rs_eofi==1)
        {
    	    ipsec_rx_packet();
//	        mark_bh(RISCOM8_BH);
    		if (status.bits.ts_eofi==0) /* Tx interrupt losed */
    		{
                ipsec_complete_tx_packet();
    	    }
    	}
    }	
	//enable_irq(IRQ_IPSEC);
	return IRQ_RETVAL(handled);
}     

static int ipsec_interrupt_polling(void)
{
	IPSEC_DMA_STATUS_T	status;
	unsigned int        i;

    for (i=0;i<40001;i++)
    {
        /* read DMA status */
	    status.bits32 = ipsec_read_reg(IPSEC_DMA_STATUS);

        if (status.bits.rs_eofi==1)
        {
    	    /* clear DMA status */
            ipsec_write_reg(IPSEC_DMA_STATUS,status.bits32,status.bits32);  
            break;
        }
        if (i>40000)
        {
            ipsec_read_reg(0x0000);
            printk("FCS fail.......\n");
            return -1;
        }
    }

    if ((status.bits32 & 0x63000000) > 0)
    { 
        printk("Error :");       
    	if (status.bits.ts_derr==1)
    	{
    	    printk("AHB bus Error While Tx !!!\n");
            return -2;
    	}
    	if (status.bits.ts_perr==1)
    	{
    	    printk("Tx Descriptor Protocol Error !!!\n");
            return -3;
    	}    
    	if (status.bits.rs_derr==1)
    	{
    	    printk("AHB bus Error While Rx !!!\n");
            return -4;
    	}
    	if (status.bits.rs_perr==1)
    	{
    	    printk("Rx Descriptor Protocol Error !!!\n");
            return -5;
    	}  
    }
    	
    if (status.bits.ts_eofi==1)
    {
        ipsec_complete_tx_packet();
    }    
    if (status.bits.rs_eofi==1)
    {
	    ipsec_rx_packet();
		if (status.bits.ts_eofi==0) /* Tx interrupt losed */
		{
            ipsec_complete_tx_packet();
	    }
	}
	//enable_irq(IRQ_IPSEC);
	return 0;    
}     

static void ipsec_byte_change(unsigned char *in_key,unsigned int in_len,unsigned char *out_key,unsigned int *out_len)
{
    unsigned int    i,j;
    
    memset(out_key,0x00,sizeof(out_key));
    *out_len = ((in_len + 3)/4) * 4;
    for (i=0;i<(*out_len/4);i++)
    {
        for (j=0;j<4;j++)
        {
            out_key[i*4+(3-j)] =  in_key[i*4+j];
        }
    }        
}

static void ipsec_fcs_init(void)
{      
    memset(&fcs_op,0x00,sizeof(fcs_op));
    fcs_op.op_mode = AUTH;
    fcs_op.auth_algorithm = FCS;
    fcs_op.auth_result_mode = CHECK_MODE; 
    fcs_op.callback = NULL;
    fcs_op.auth_header_len = 0;
#if 1    
    memset(&fcs_auth,0x00,sizeof(IPSEC_AUTH_T));
    fcs_auth.var.control.bits.op_mode = fcs_op.op_mode;    /* authentication */
    fcs_auth.var.control.bits.auth_mode = fcs_op.auth_result_mode;  /* append/check authentication result  */
    fcs_auth.var.control.bits.auth_algorithm = fcs_op.auth_algorithm; /* FCS */
    fcs_auth.var.control.bits.auth_check_len = 4; /* 4-word to be checked or appended */
#endif
}

#ifdef CONFIG_SL2312_HW_CHECKSUM
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
{
    static unsigned int     pid = 0;
    unsigned int            checksum=0;
    
    if (len < MIN_HW_CHECKSUM_LEN)
    {
        checksum = csum_partial_sw(buff,len,sum);
    }
    else
    {
//        fcs_op.process_id = (pid++) % 256;
        fcs_op.in_packet = (unsigned char *)buff;
        fcs_op.pkt_len = len;
        fcs_op.out_packet = (unsigned char *)&out_packet[0];
        fcs_op.auth_algorithm_len = len;
        ipsec_crypto_hw_process(&fcs_op);
//interruptible_sleep_on(&ipsec_wait_q);
        checksum = fcs_op.checksum + sum;
    } 
    return (checksum);
}
unsigned int csum_partial_copy_nocheck(const char *src, char *dst, int len, int sum)
{
    unsigned int            checksum;

    if (len < MIN_HW_CHECKSUM_LEN)
    {
        checksum = csum_partial_copy_nocheck_sw(src,dst,len,sum);    
    }
    else    
    {
        fcs_op.in_packet = (unsigned char *)src;
        fcs_op.pkt_len = len;
        fcs_op.out_packet = (unsigned char *)dst;
        fcs_op.auth_algorithm_len = len;
        ipsec_crypto_hw_process(&fcs_op);
        checksum = fcs_op.checksum + sum;
#if (ZERO_COPY==1)    
        memcpy(dst,src,len);		
#endif        
    }
    return (checksum);
}    


int ipsec_checksum_test(void)
{
    unsigned int    i,j;
    unsigned int    t1,t2;
    unsigned int    sum1,sum2;
    unsigned char   *src;
    unsigned char   *dst;
        
    src = kmalloc(IPSEC_MAX_PACKET_LEN,GFP_ATOMIC);
    dst = kmalloc(IPSEC_MAX_PACKET_LEN,GFP_ATOMIC);

    for(i=0;i<IPSEC_MAX_PACKET_LEN;i++)
    {
        src[i]=i%256;
    }
        
    for (i=64;i<=2048;i=i+64)
    {
        t1 = jiffies;
        for (j=0;j<100000;j++)
        {
            sum1=csum_partial_copy_nocheck_sw(src, dst, i, 0);
        }
        t2 = jiffies;
        sum1 = (sum1 >> 16) + (sum1 & 0x0000ffff);
        if (sum1 > 0xffff)  sum1 = (sum1 & 0x0000ffff) + 1;
        printk("S/W len=%04d sum=%04x time=%04d<===>",i,sum1,t2-t1);

        t1 = jiffies;
        for (j=0;j<100000;j++)
        {
            sum2=csum_partial_copy_nocheck(src, dst, i, 0);
        }
        t2 = jiffies;
        printk("H/W(A) len=%04d sum=%04x time=%04d",i,sum2,t2-t1);
        if (sum1 == sum2)
        {
            printk ("---OK!\n");
        }
        else
        {
            printk("---FAIL!\n");    
        }    
    }

    return (0);        
}
#endif

int ipsec_get_cipher_algorithm(unsigned char *alg_name,unsigned int alg_mode)
{
    static unsigned char name[3][8]={"des","des3_ede","aes"};
    static unsigned int  algorithm[2][3]={{ECB_DES,ECB_3DES,ECB_AES},{CBC_DES,CBC_3DES,CBC_AES}};
    unsigned int         i;

    if ((alg_mode != ECB) && (alg_mode != CBC))
        return -1;
        
    for (i=0;i<3;i++)
    {
        if (strncmp(alg_name,&name[i][0],8) == 0)
        {
            return (algorithm[alg_mode][i]);
        }    
    }
    return -1;
}

int ipsec_get_auth_algorithm(unsigned char *alg_name,unsigned int alg_mode)
{
    static unsigned char name[2][8]={"md5","sha1"};
    static unsigned int  algorithm[2][2]={{MD5,HMAC_MD5},{SHA1,HMAC_SHA1}};
    unsigned int         i;

    //if ((alg_mode != 0) && (alg_mode != 1))
    //    return -1;
        
    for (i=0;i<2;i++)
    {
        if (strncmp(alg_name,&name[i][0],8) == 0)
        {
            	return (algorithm[i][alg_mode]);
        }    
    }
    return -1;
}
        
static int __init ipsec_initial(void)
{

	printk ("ipsec_init : cryptographic accelerator \n");
        
    ipsec_queue = &dummy[2];
	ipsec_queue->next = ipsec_queue->prev = ipsec_queue;

    ipsec_fcs_init();    
    ipsec_buf_init();
    ipsec_hw_start();
    
	/* Install interrupt request */
	//request_irq(IRQ_IPSEC,ipsec_interrupt,0,"SL2312-IPSEC",NULL);

#if 0
for (;;)
{
    unsigned int t1,t2;
    
    t1 = ipsec_get_time();
//    ipsec_checksum_test();
/*    ipsec_adv_auth_fix_algorithm_test();
    ipsec_adv_auth_vary_algorithm_test();
    ipsec_adv_cipher_fix_algorithm_test();
    ipsec_adv_cipher_vary_algorithm_test();
    ipsec_adv_auth_then_decrypt_test();
*/    ipsec_adv_encrypt_then_auth_test();
    
    t2 = ipsec_get_time();
    printk("Time = %d \n",t1-t2);
}
#endif
	return 0;
}

static void __exit ipsec_cleanup (void)
{
	free_irq(IRQ_IPSEC,NULL);
	if (tp->tx_desc)
	{
		DMA_MFREE(tp->tx_desc, TX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(unsigned int)tp->tx_desc_dma);
	}
	if (tp->rx_desc)
	{
		DMA_MFREE(tp->rx_desc, RX_DESC_NUM*sizeof(IPSEC_DESCRIPTOR_T),(unsigned int)tp->rx_desc_dma);
    }
}

module_init(ipsec_initial);
module_exit(ipsec_cleanup);

 