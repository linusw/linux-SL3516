/***********************************************************************
 * Copyright 2007 StorLink Semiconductors, Inc.  All rights reserved.
 *---------------------------------------------------------------------
 * Name:		sl351x_ipsec.c
 * Description:	IPSEC-VPN implementation by using classification queue
 *				and HW Crypto Engine, VPN environment variables are 
 *				set up by setkey, racoon, and GUI.  What this level is
 *				handling is the transaction of encrypting/decrypting
 *				given VPN packets.
 *				3 flags / modes are provided.
 *				1) CONFIG_CRYPTO_BATCH: process packet by batch. Performance
 *						is better. (Turn it on!!)
 *					If turn off: process packet 1 by 1.
 *				2) CONFIG_SL351X_IPSEC_WITH_WIFI: case that LAN interface will be
 *						a virtual bridge device that contains a WiFi interface and 
 *						a GMAC.  Several checks have to be included.
 *				3) CONFIG_SL351X_IPSEC_REUSE_SKB: this mode is to re-use the memory
 *						that's been allocated for the received skb.  Default is 
 *						"OFF." Due to some safety issues, it's better to turn it off.
 * Note:	NAPI mode for classification queue has been merged into sl351x_gmac. 
 *			The performance with NAPI mode enabled is more tested, and should 
 *			run faster than usual interrupt mode. 
 * History:
 *
 * ------------------------------------------------------------
 * Feng Liu: Original Implementation
 * Wen Hsu: 
 *------------------------------------------------- */
#define CONFIG_SL_NAPI			1	// NAPI mode support. inherit from sl351x_gmac

/*******************
 * Mode Selections 
 ******************/
#define CONFIG_CRYPTO_BATCH
#define CONFIG_SL351X_IPSEC_WITH_WIFI
//#define CONFIG_SL351X_IPSEC_REUSE_SKB

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
//#include <linux/timer.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/sysctl_storlink.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/sysctl.h>
#include <linux/netfilter_ipv4.h>
//#include <linux/interrupt.h>
#include <net/arp.h>
#include <net/xfrm.h>
//#include <asm/io.h>
//#include <asm/arch/irqs.h>
#include <asm/arch/sl2312_ipsec.h>
#include <asm/arch/sl351x_gmac.h>
#include <asm/arch/sl351x_hash_cfg.h>
#include <asm/arch/sl351x_ipsec.h>
#include "/source/kernel/linux/net/bridge/br_private.h"


/************************
 * Constant Definition  *
 ************************/
#define		IP_HEADER_SIZE		sizeof(struct iphdr)

/************************
 * Variable Declaration *
 ************************/
struct IPSEC_VPN_TUNNEL_CONFIG ipsec_tunnel[MAX_IPSEC_TUNNEL];
struct IPSEC_VPN_IP_PAIR_CONFIG ipsec_pair[MAX_IPSEC_TUNNEL];
static int class_rule_initialized = 0;
static DEFINE_RWLOCK(ipsec_tunnel_lock);
static __u32 local_IP = 0;

#ifdef CONFIG_CRYPTO_BATCH
struct IPSEC_PACKET_S CRYPTO_QUEUE[CRYPTO_QUEUE_SIZE];
static int current_crypto_loc = 0;
static int current_crypto_rptr = 0;
static int crypto_queue_count = 0;
#endif
static unsigned long count_encrypted = 0;
static unsigned long count_decrypted = 0;
static int packet_error = 0;
extern unsigned int Giga_switch;
static struct sk_buff * old_skb_queue_head;

/************************
 * Function Declaration *
 ************************/
int sl351x_ipsec_init(void);
void ipsec_init_class_queue(void);
static int hash_add_ipsec_entry(HASH_ENTRY_T *entry);
static int create_ipsec_hash_entry(struct IPSEC_VPN_TUNNEL_CONFIG *tunnel_config, int qid);
int ipsec_handle_class_queue(struct net_device * dev, GMAC_INFO_T *tp, int budget);
void ipsec_finish_callback(struct IPSEC_PACKET_S *ipsec_ptr);
int ipsec_gmac_process(struct sk_buff *skb, unsigned int sw_id);
int ipsec_gmac_callback(struct sk_buff *skb, struct IPSEC_VPN_TUNNEL_CONFIG *ipsec_tunnel_ptr,struct sk_buff *old_skb,int flag_polling);
int ipsec_handle_skb(struct sk_buff *skb, unsigned int sw_id, unsigned int clone_flag);
int ipsec_handle_skb_finish(void);

void ipsec_vpn_tunnel_start(void);
int disable_vpn_hash(struct IPSEC_VPN_TUNNEL_CONFIG *tunnel_ptr);
static int skb_send_to_kernel(struct sk_buff *skb);
static int vpn_sysctl_info(ctl_table *ctl, int write, struct file * filp,
                           void __user *buffer, size_t *lenp, loff_t *ppos);

extern int mac_set_rule_reg(int mac, int rule, int enabled, u32, u32, u32);
extern void gmac_write_reg(unsigned int base, unsigned int offset,unsigned int data,unsigned int bit_mask);
extern void toe_gmac_fill_free_q(void);
extern int sl_ip_route_cache(struct sk_buff *skb, u32 daddr, u32 saddr, u8 tos, struct net_device *dev, int iif, int oif);

//extern void hash_invalidate_entry(int index);
extern void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);
extern int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer);

static struct ctl_table_header *vpn_table_header;
#define VPN_INFO_BUFFER_SIZE 8*MAX_IPSEC_TUNNEL

static int vpn_info[VPN_INFO_BUFFER_SIZE];

// /proc/sys/dev/vpn/vpn_pair
static ctl_table vpn_table[] = {
	{
		.ctl_name       = NET_VPN_Pair,
		.procname       = "vpn_pair",
		.data           = vpn_info,
		.maxlen         = 9*MAX_IPSEC_TUNNEL*sizeof(int),
		.mode           = 0644,
		.proc_handler   = &vpn_sysctl_info,
	},
	{ .ctl_name = 0 }
};

static ctl_table vpn_dir_table[] = {
	{
		.ctl_name       = NET_VPN,
		.procname       = "vpn",
		.maxlen         = 0,
		.mode           = 0555,
		.child          = vpn_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table vpn_net_table[] = {
	{
		.ctl_name       = CTL_NET,
		.procname       = "net",
		.maxlen         = 0,
		.mode           = 0555,
		.child          = vpn_dir_table,
	},
	{ .ctl_name = 0 }
};

#ifdef CONFIG_SL351X_DUALCORE_VPN
static int getcpuid(void)
{
	int cpuid;

	__asm__(
"mrc p8, 0, r0, c0, c0, 0\n"
"mov %0, r0"
       :"=r"(cpuid)
       :
       :"r0");
       return (cpuid & 0x07);
}
#endif

/*--------------------------------------------------------------------------
 * sl351x_ipsec_init()
 * Description: setup matching fields of classification queue for sl351x_ipsec
 * Status: Matching rules should work now. Problem is for GMAC0 (WAN port), I've
 *			not defined the SPR for ESP and AH. (but it still works for some reasons).
 *-------------------------------------------------------------------------*/
int sl351x_ipsec_init(void)
{
#ifdef CONFIG_SL351X_DUALCORE_VPN
	if (getcpuid() == 0)
	{
#endif
    GMAC_MRxCR0_T   mrxcr1_0, mrxcr0_0;
    GMAC_MRxCR1_T   mrxcr1_1, mrxcr0_1;
    GMAC_MRxCR2_T   mrxcr1_2, mrxcr0_2;
//	unsigned int *addr;
    int result;
    int rule = CONFIG_SL351x_IPSEC_RULE_ID;

	if (class_rule_initialized)
		return 0;

	class_rule_initialized = 1;
	printk("%s::Setting up Matching rule\n",__func__);
	// LAN
    mrxcr0_0.bits32 = 0;
	mrxcr0_1.bits32 = 0;
	mrxcr0_2.bits32 = 0;
	// check the matching rule..

    mrxcr0_0.bits.l3 = 1;
	mrxcr0_0.bits.l4 = 0;		// no need lv4. 
//	mrxcr0_0.bits.sprx = 0x0f;	// no spr.
//	mrxcr0_0.bits.sprx = 0x01;
    mrxcr0_1.bits.sip = 1;
    mrxcr0_1.bits.dip = 1;
	mrxcr0_1.bits.sip_netmask = 0x18;	// 08 or 18? seems like 18, for netmask 255.255.255.0
	mrxcr0_1.bits.dip_netmask = 0x18;	// it says 0x08 on the TOE doc, but it seems wrong

	if (Giga_switch)
		result = mac_set_rule_reg(1, rule, 1, mrxcr0_0.bits32, mrxcr0_1.bits32, mrxcr0_2.bits32);
	else
		result = mac_set_rule_reg(0, rule, 1, mrxcr0_0.bits32, mrxcr0_1.bits32, mrxcr0_2.bits32);
    if (result < 0) {
        printk("%s: set eth0 rule fail\r\n", __func__);
        return ERR_MATCH_RULE;
    }

	// WAN
    mrxcr1_0.bits32 = 0;
	mrxcr1_1.bits32 = 0;
	mrxcr1_2.bits32 = 0;
	// check the matching rule..

    mrxcr1_0.bits.l3 = 1;
	mrxcr1_0.bits.l4 = 0;			// up for ESP/AH
	mrxcr1_0.bits.sprx = 0x08;		// enable SPR3 for ESP. (not enable yet)SPR4 for AH
    mrxcr1_1.bits.sip = 1;
    mrxcr1_1.bits.dip = 1;

	if (Giga_switch)
		result = mac_set_rule_reg(0, rule, 1, mrxcr1_0.bits32, mrxcr1_1.bits32, mrxcr1_2.bits32);
	else
		result = mac_set_rule_reg(1, rule, 1, mrxcr1_0.bits32, mrxcr1_1.bits32, mrxcr1_2.bits32);
	if (result < 0) {
        printk("%s: set eth1 rule fail\r\n", __func__);
        return ERR_MATCH_RULE;
    }
    
#ifdef CONFIG_CRYPTO_BATCH
	memset(CRYPTO_QUEUE, 0, CRYPTO_QUEUE_SIZE*sizeof(struct IPSEC_PACKET_S));
//	CRYPTO_QUEUE = (struct IPSEC_PACKET_S*)kmalloc(CRYPTO_QUEUE_SIZE*sizeof(struct IPSEC_PACKET_S),GFP_ATOMIC);
	current_crypto_loc = 0;
#endif
	// initiatition of pair structure
	memset(ipsec_pair, 0, MAX_IPSEC_TUNNEL*sizeof(struct IPSEC_VPN_IP_PAIR_CONFIG));
	vpn_table_header = register_sysctl_table(vpn_net_table, 1);

//	spin_lock_init(&ipsec_skb_handle_lock);
//	spin_lock_init(&ipsec_ptr_handle_lock);
#ifdef CONFIG_SL351X_DUALCORE_VPN
	}
#endif
    return 1;
} 

/*-----------------------------------------------------------------
 * ipsec_init_class_queue
 * Description: to initialize class queue that's going be used for
 *				this module
 *				only need 2 class queues, (#0 and #1), but still enables
 *				all 12 class queues
 *----------------------------------------------------------------*/
void ipsec_init_class_queue(void)
{
#ifdef CONFIG_SL351X_DUALCORE_VPN
	if (getcpuid() == 0)
	{
#endif
	TOE_INFO_T		*toe = &toe_private_data;
	NONTOE_QHDR_T	*qhdr;
	GMAC_RXDESC_T	*desc_ptr;
	int		i;

	qhdr = (NONTOE_QHDR_T*)TOE_CLASS_Q_HDR_BASE;
	desc_ptr = (GMAC_RXDESC_T*)DMA_MALLOC((TOE_CLASS_QUEUE_NUM * TOE_CLASS_DESC_NUM
		*sizeof(GMAC_RXDESC_T)), (dma_addr_t*)&toe->class_desc_base_dma);

	if (!desc_ptr) {
		printk("%s::DMA_MALLOC classificaiton queue fail!\n", __func__);
		return;
	}
	memset((void*)desc_ptr, 0, TOE_CLASS_QUEUE_NUM * TOE_CLASS_DESC_NUM *
		sizeof(GMAC_RXDESC_T));
	toe->class_desc_base = (unsigned int)desc_ptr;
	toe->class_desc_num = TOE_CLASS_DESC_NUM;

	for (i=0; i<TOE_CLASS_QUEUE_NUM; i++, qhdr++) {
		qhdr->word0.base_size = (((unsigned int)toe->class_desc_base_dma +
			i*(TOE_CLASS_DESC_NUM)*sizeof(GMAC_RXDESC_T)) & NONTOE_QHDR0_BASE_MASK)
			| TOE_CLASS_DESC_POWER;
		qhdr->word1.bits32 = 0;
	}
#ifdef CONFIG_SL351X_DUALCORE_VPN
	}
#endif
}

/* ------------------------------------------------------------------*
 * ipsec_vpn_tunnel_start
 * Description: API for setkey to send the needed info to driver
 * ------------------------------------------------------------------*/
void ipsec_vpn_tunnel_start(void)
{
	struct IPSEC_VPN_TUNNEL_CONFIG * tunnel_ptr;
	struct IPSEC_VPN_IP_PAIR_CONFIG * pair_ptr;
	int count, count_pair, qid;
	int hash_entry_index = -1;
	// first check the tunnel config with pair config
	// for each tunnel config, start the hash. also,
	// complete the tunnel config with info from pair config

	tunnel_ptr = ipsec_tunnel;
	count = 0;

	while ((tunnel_ptr != NULL) && (count < MAX_IPSEC_TUNNEL))
	{
		if ((tunnel_ptr->enable == 1) && (tunnel_ptr->sa_hash_flag == 0))
		{
			pair_ptr = ipsec_pair;
			count_pair = 0;
			while ((pair_ptr != NULL) && (count_pair < MAX_IPSEC_TUNNEL))
			{
				if ((pair_ptr->enable == 1) && 
					(pair_ptr->src_WAN_IP == tunnel_ptr->src_WAN_IP) && 
					(pair_ptr->dst_WAN_IP == tunnel_ptr->dst_WAN_IP))	// find the matching pair
				{
					// complete the tunnel config
					tunnel_ptr->src_LAN = pair_ptr->src_LAN & pair_ptr->src_netmask;
					tunnel_ptr->src_netmask = pair_ptr->src_netmask;
					tunnel_ptr->dst_LAN = pair_ptr->dst_LAN & pair_ptr->dst_netmask;
					tunnel_ptr->dst_netmask = pair_ptr->dst_netmask;
					tunnel_ptr->src_LAN_GW = pair_ptr->src_LAN_GW;
					tunnel_ptr->mode = pair_ptr->direction;
					tunnel_ptr->tableID = count;
					
					// how about checking of pair_ptr->direction?
					if (tunnel_ptr->mode == 0)		// encryption, go IPSEC_TEST_OUTBOUD_QID
					{
						qid = IPSEC_TEST_OUTBOUND_QID;
					}
					else // if (tunnel_ptr->mode == 1)		// decryption
					{
						qid = IPSEC_TEST_INBOUND_QID;
					}

					// create hash and start it.
					hash_entry_index = create_ipsec_hash_entry(tunnel_ptr, qid);
					if (hash_entry_index <0) {
						printk("%s::release classification queue hash entry %d!\n", __func__, qid);
					return;
					}
					printk("%s::class qid %d, hash_entry_index %x\n", __func__, qid, hash_entry_index);
					tunnel_ptr->sa_hash_entry = (__u16)hash_entry_index;
					//hash_set_valid_flag(hash_entry_index, tunnel_ptr->enable);
					hash_set_valid_flag(hash_entry_index, 1);
					tunnel_ptr->sa_hash_flag = 1;
					tunnel_ptr->connection_count = 0;
					break;
				}
				pair_ptr++;
				count_pair++;
			}
		}
		tunnel_ptr++;
		count++;
	}
}

/* ------------------------------------------------------------------*
 * disable_vpn_hash
 * Description: API for setkey and racoon to disable / delete certain VPN
 *				tunnel (deleting is done in esp4.c)
 * ------------------------------------------------------------------*/
int disable_vpn_hash(struct IPSEC_VPN_TUNNEL_CONFIG *tunnel_ptr)
{
//	int count;

	hash_set_valid_flag(tunnel_ptr->sa_hash_entry, 0);
	hash_invalidate_entry(tunnel_ptr->sa_hash_entry);
	
	// also clean the queue?
	return 0;
}

static int hash_add_ipsec_entry(HASH_ENTRY_T *entry)
{
    int rc;
    u32 key[HASH_MAX_DWORDS];

    rc = hash_build_keys((u32 *)&key, entry);
    if (rc < 0)
        return -1;
  
    hash_write_entry(entry, (unsigned char *)&key[0]);

    return entry->index;
}

/*----------------------------------------------------------------------
 * Name: create_ipsec_hash_entry
 * Description: to create a new hash entry
 *---------------------------------------------------------------------*/
static int create_ipsec_hash_entry(struct IPSEC_VPN_TUNNEL_CONFIG *tunnel_config, int qid)
{
    HASH_ENTRY_T hash_entry;
    int hash_entry_index;
	
    memset((void *)&hash_entry, 0, sizeof(HASH_ENTRY_T));

    //hash_entry.rule = 0;					// original
	hash_entry.rule = CONFIG_SL351x_IPSEC_RULE_ID;
	hash_entry.key_present.ip_protocol = 0;
	//hash_entry.key_present.da = 1;		// it's On in the TOE Design Document
	//hash_entry.key_present.sa = 1;		// it's On in the TOE Design Document
    hash_entry.key_present.sip = 1;
	hash_entry.key_present.dip = 1;
	hash_entry.key_present.l7_bytes_0_3 = 0;
    hash_entry.key_present.l7_bytes_4_7 = 0;
	hash_entry.key_present.port = 0;		// ? on? because it's always from the same GMAC port
	hash_entry.key_present.Ethertype = 0;

	if (qid == IPSEC_TEST_OUTBOUND_QID)
	{
		hash_entry.key_present.l4_bytes_0_3 = 0;
	//	hash_entry.key.port = GMAC_PORT0;
		hash_entry.key.sip[3] = IPIV4(tunnel_config->src_LAN);
		hash_entry.key.sip[2] = IPIV3(tunnel_config->src_LAN);
		hash_entry.key.sip[1] = IPIV2(tunnel_config->src_LAN);
		hash_entry.key.sip[0] = IPIV1(tunnel_config->src_LAN);
		hash_entry.key.dip[3] = IPIV4(tunnel_config->dst_LAN);
		hash_entry.key.dip[2] = IPIV3(tunnel_config->dst_LAN);
		hash_entry.key.dip[1] = IPIV2(tunnel_config->dst_LAN);
		hash_entry.key.dip[0] = IPIV1(tunnel_config->dst_LAN);

		// do we need value for hash_entry.param ? it doesn't seem like it matters
		hash_entry.param.Sip = tunnel_config->src_LAN;		// it should be "host" not network style(?)
		hash_entry.param.Dip = tunnel_config->dst_LAN;		// also, need to "and" with netmask(?)
		hash_entry.param.Sport = 0;
		hash_entry.param.Dport = 0;
		hash_entry.param.vlan = 0;
		hash_entry.param.sw_id = tunnel_config->tableID;
//		hash_entry.param.mtu = 0;
	}
	else if (qid == IPSEC_TEST_INBOUND_QID)
	{
		hash_entry.key_present.ip_protocol = 1;
		hash_entry.key.ip_protocol = tunnel_config->protocol;
		hash_entry.key_present.l4_bytes_0_3 = 0;
		//hash_entry.key_present.l4_bytes_0_3 = 1;
		//memcpy((void *)hash_entry.key.l4_bytes, &connection->spi, 4);
	//	hash_entry.key.port = GMAC_PORT1;
		hash_entry.key.sip[0] = IPIV1(tunnel_config->src_WAN_IP);
		hash_entry.key.sip[1] = IPIV2(tunnel_config->src_WAN_IP);
		hash_entry.key.sip[2] = IPIV3(tunnel_config->src_WAN_IP);
		hash_entry.key.sip[3] = IPIV4(tunnel_config->src_WAN_IP);
		// debug message
		//printk("connection->src_ip = %d.%d.%d.%d\n",IPIV4(connection->src_ip),IPIV3(connection->src_ip),
		//				IPIV2(connection->src_ip),IPIV1(connection->src_ip));
		hash_entry.key.dip[0] = IPIV1(tunnel_config->dst_WAN_IP);
		hash_entry.key.dip[1] = IPIV2(tunnel_config->dst_WAN_IP);
		hash_entry.key.dip[2] = IPIV3(tunnel_config->dst_WAN_IP);
		hash_entry.key.dip[3] = IPIV4(tunnel_config->dst_WAN_IP);

		hash_entry.param.Sip = tunnel_config->src_WAN_IP;
		hash_entry.param.Dip = tunnel_config->dst_WAN_IP;
		hash_entry.param.Sport = 0;
		hash_entry.param.Dport = 0;
		hash_entry.param.vlan = 0;
		hash_entry.param.sw_id = tunnel_config->tableID;
	}
	else
		printk("Shouldn't get here !\n");

	hash_entry.key.Ethertype = 0;
	hash_entry.key.ipv6 = 0;
	hash_entry.key.ip_tos = 0;
	hash_entry.key.vlan_id = 0;
	hash_entry.action.sw_id = 1;
    hash_entry.action.dest_qid = TOE_CLASSIFICATION_QID(qid);
    hash_entry.action.srce_qid = 0;

    hash_entry_index = hash_add_ipsec_entry(&hash_entry);
        
    return hash_entry_index;
}

/* ---------------------------------------------------------------------------
 * sl_fast_ipsec()
 * Description: the method which checks the skb with the valid hardware-enabled 
 *				ipsec-vpn tunnel.  If it matches, this packet will be sent to
 *				hardware VPN path.
 * Return 1, if current packet will be handled by hardware VPN. 0, otherwise.
 * --------------------------------------------------------------------------*/
int sl_fast_ipsec(struct sk_buff *skb) {
	int i;

	struct iphdr *iph;

	iph = (struct iphdr *)(skb->data + ETH_HLEN); 

//	printk("%s packet received from skb->dev %s\n", __func__, skb->dev->name);
	
	for(i=0; i<MAX_IPSEC_TUNNEL; i++) {

		if(ipsec_tunnel[i].enable == 0) {
//			printk("tunnel %d enable == 0\n", i);
			continue;
		}

		// check for packets that are coming from LAN
/*
		printk("ipsec_tunnel[i].src_LAN %x\n", ipsec_tunnel[i].src_LAN);
		printk("ipsec_tunnel[i].dst_LAN %x\n", ipsec_tunnel[i].dst_LAN);
		printk("ipsec_tunnel[i].src_netmask %x\n", ipsec_tunnel[i].src_netmask);

		printk("ntohl(iph->saddr) %x\n", ntohl(iph->saddr));		
		printk("ntohl(iph->daddr) %x\n", ntohl(iph->daddr));		
*/		
		if ( 
		(ipsec_tunnel[i].src_LAN == (ntohl(iph->saddr) & ipsec_tunnel[i].src_netmask))
		&& (ipsec_tunnel[i].dst_LAN  == (ntohl(iph->daddr) & ipsec_tunnel[i].dst_netmask))
		) {
			ipsec_handle_skb(skb, i, 1);
//			printk("%s - packet handled 1\n", __func__);
			return 1;
		}

		// check for packets that are coming from WAN
		if (iph->protocol == ipsec_tunnel[i].protocol
			&& ipsec_tunnel[i].src_WAN_IP == ntohl(iph->saddr)
			&& ipsec_tunnel[i].dst_WAN_IP == ntohl(iph->daddr)) {

			ipsec_handle_skb(skb, i, 1);
//			printk("%s - packet handled 2\n", __func__);
			return 1;
		}

	}
//	if(printk_ratelimit()) {
//		printk("%s : %s packet going slow vpn path\n", __func__, skb->dev->name);
//	}
	return 0;
}
EXPORT_SYMBOL(sl_fast_ipsec);

/* -----------------------------------------------------------------------------
 * skb_send_to_kernel()
 * Description: a simple function that transforms the skb and sends it to kernel
 * ----------------------------------------------------------------*/
static int skb_send_to_kernel(struct sk_buff *skb)
{
	skb->protocol = eth_type_trans(skb,skb->dev);
#ifdef CONFIG_SL_NAPI
	if (storlink_ctl.napi == 1)
		return netif_receive_skb(skb);
	else
#endif
		return netif_rx(skb);
}

/*------------------------------------------------------------------------
 * ipsec_handle_class_queue
 * Description: to handle packet that's being received by the class queue.
 *				upon successfully receiving, it either passes it to 
 *				ipsec_gmac_process to handle it, or put it in queue in tasklet
 *				and polling mode.  The interrupt routine in GMAC driver will 
 *				call this function upon receiving packets in class queue.
 *------------------------------------------------------------------------*/
int ipsec_handle_class_queue(struct net_device * dev, GMAC_INFO_T *tp, int budget)
{
	TOE_INFO_T		*toe = &toe_private_data;
	volatile GMAC_RXDESC_T	*curr_desc;
	struct sk_buff	*skb;
	volatile DMA_RWPTR_T		rwptr;
	unsigned int	pkt_size;
	int		i;
	unsigned int	desc_count;
	unsigned int	good_frame, chksum_status, rx_status;
	struct net_device_stats *isPtr = (struct net_device_stats *)&tp->ifStatics;
	volatile NONTOE_QHDR_T	*class_qhdr = (NONTOE_QHDR_T*)TOE_CLASS_Q_HDR_BASE;
	unsigned int	desc_base;
	struct iphdr *ip_hdr;
	int rx_pkt_num=0;
	unsigned int sw_id;

	if (Giga_switch) {
		if (dev == toe->gmac[1].dev) {
			class_qhdr += IPSEC_TEST_OUTBOUND_QID;
			desc_base = (unsigned int)toe->class_desc_base + (unsigned int)IPSEC_TEST_OUTBOUND_QID*TOE_CLASS_DESC_NUM*sizeof(GMAC_RXDESC_T);
			isPtr = (struct net_device_stats *)&toe->gmac[1].ifStatics;
		}
		else if (dev == toe->gmac[0].dev) {
			class_qhdr += IPSEC_TEST_INBOUND_QID;
			desc_base = (unsigned int)toe->class_desc_base + (unsigned int)IPSEC_TEST_INBOUND_QID*TOE_CLASS_DESC_NUM*sizeof(GMAC_RXDESC_T);
			isPtr = (struct net_device_stats *)&toe->gmac[0].ifStatics;
		}
		else
			printk("%s::no device!?!?1\n",__func__);
	}
	else // if (Giga_switch == 0)
	{
		if (dev == toe->gmac[1].dev) {
			class_qhdr += IPSEC_TEST_INBOUND_QID;
			desc_base = (unsigned int)toe->class_desc_base + (unsigned int)IPSEC_TEST_INBOUND_QID*TOE_CLASS_DESC_NUM*sizeof(GMAC_RXDESC_T);
			isPtr = (struct net_device_stats *)&toe->gmac[1].ifStatics;
		}
		else if (dev == toe->gmac[0].dev) {
			class_qhdr += IPSEC_TEST_OUTBOUND_QID;
			desc_base = (unsigned int)toe->class_desc_base + (unsigned int)IPSEC_TEST_OUTBOUND_QID*TOE_CLASS_DESC_NUM*sizeof(GMAC_RXDESC_T);
			isPtr = (struct net_device_stats *)&toe->gmac[0].ifStatics;
		}
		else
			printk("%s::no device!?!?1\n",__func__);
	}
	rwptr.bits32 = readl(&class_qhdr->word1);

#ifdef CONFIG_SL_NAPI
	while ((rwptr.bits.rptr != rwptr.bits.wptr) &&
		(((storlink_ctl.napi == 1) && (rx_pkt_num < budget)) || 
			(storlink_ctl.napi == 0))) {
#else
	while (rwptr.bits.rptr != rwptr.bits.wptr) {
#endif
#ifdef CONFIG_CRYPTO_BATCH
		// if ipsec packet queue is full, do not read any packet from
		// classification queue. just skip this, and try to put the packets
		// into crypto engine's TX queue
		if (CRYPTO_QUEUE[current_crypto_loc].used == 1)
			goto queue_full;
#endif
		curr_desc = (GMAC_RXDESC_T*)(desc_base +
			(unsigned int)rwptr.bits.rptr * sizeof(GMAC_RXDESC_T));
		tp->rx_curr_desc = (unsigned int)curr_desc;
		rx_status = curr_desc->word0.bits.status;
		chksum_status = curr_desc->word0.bits.chksum_status;
		tp->rx_status_cnt[rx_status]++;
		tp->rx_chksum_cnt[chksum_status]++;
		desc_count = curr_desc->word0.bits.desc_count;
		pkt_size = curr_desc->word1.bits.byte_count;
		good_frame = 1;

		if ((curr_desc->word0.bits32 & (GMAC_RXDESC_0_T_derr | GMAC_RXDESC_0_T_perr))
			|| (chksum_status & 0x4)
			|| rx_status)
		{
			good_frame = 0;
			if (rx_status)
			{
				if (rx_status == 4 || rx_status == 7)
					isPtr->rx_crc_errors++;
			}
			skb = (struct sk_buff*)(REG32(__va(curr_desc->word2.buf_adr)-SKB_RESERVE_BYTES));
			if (skb->destructor)
				dev_kfree_skb_any(skb);
			else
				dev_kfree_skb(skb);
			skb = NULL;
		}

		if (good_frame)
		{
			if (curr_desc->word0.bits.drop)
				printk("%s::Drop (GMAC-%d)!!!\n", __func__, tp->port_id);
			skb = (struct sk_buff*)(REG32(__va(curr_desc->word2.buf_adr)-SKB_RESERVE_BYTES));
			if (!skb)
			{
				printk("Fatal Error!! skb==NULL!\n");
				goto next_rx;
			}
			isPtr->rx_packets++;
			//if ((skb->len+pkt_size) > (SW_RX_BUF_SIZE+16)) {
			if ((skb->len+pkt_size) > (1514+16)) {
				printk("%s::error in skb allocation (most likely)\n",__func__);
				printk("%s::skb len %d, pkt_size %d\n", __func__, skb->len, pkt_size);
				// skb->len should equal skb->tail-skb->data
				// skb->truesize equals skb->end - skb->head
				printk("skb->len = skb->tail - skb->data = %d\n", skb->tail - skb->data);
				printk("skb->truesize = skb->end - skb->head = %d\n", skb->end - skb->head);
				if (skb->destructor)
					dev_kfree_skb_any(skb);
				else
					dev_kfree_skb(skb);
				skb = NULL;
			}
			else
			{
				sw_id = curr_desc->word1.bits.sw_id;
				skb_reserve(skb, RX_INSERT_BYTES);

				if (skb->len != 0)
				{
					printk("%s::skb->len=%d\n",__func__,skb->len);
					skb->len = 0;
				}

				skb_put(skb, pkt_size);
				ip_hdr = (struct iphdr*)(skb->data+ENET_HEADER_SIZE);
				if (ntohs(ip_hdr->tot_len) < (pkt_size - ENET_HEADER_SIZE))
				{
					skb_trim(skb,ntohs(ip_hdr->tot_len)+ENET_HEADER_SIZE);
				}
					
				skb->dev = dev;
				skb->input_dev = dev;
				isPtr->rx_bytes += pkt_size;

				ipsec_handle_skb(skb, sw_id, 0);

				skb->ip_summed = CHECKSUM_UNNECESSARY;
				dev->last_rx = jiffies;
			}
		}
next_rx:
		rwptr.bits.rptr = RWPTR_ADVANCE_ONE(rwptr.bits.rptr, TOE_CLASS_DESC_NUM);
		SET_RPTR(&class_qhdr->word1, rwptr.bits.rptr);
		tp->rx_rwptr.bits32 = rwptr.bits32;
		rwptr.bits32 = readl(&class_qhdr->word1);
		rx_pkt_num++;

		toe_gmac_fill_free_q();
	}

#ifdef CONFIG_CRYPTO_BATCH
queue_full:
	ipsec_handle_skb_finish();
#endif
	return rx_pkt_num;
}

/* ----------------------------------------------------------------------------------
 * ipsec_handle_skb
 * description: it's to handle a given skb matched with specific tunnel id (sw_id), 
 *				it calls ipsec_gmac_process to find a free crypto engine packet slot
 *				in a static array and update the count in the queue. If the queue is 
 *				full, it calls process_ipsec_batch to handle the queue first.
 *				Return 0, if success.  And other value, if otherwise.
 * --------------------------------------------------------------------------------*/
int ipsec_handle_skb(struct sk_buff *skb, unsigned int sw_id, unsigned int clone_flag) 
{

	struct iphdr *ip_hdr = (struct iphdr*)(skb->data+ENET_HEADER_SIZE);
	int result;

#ifdef CONFIG_SL351X_IPSEC_WITH_WIFI
	// for packets origing from a device that's a port of a bridge, we need to change skb->dev to the bridge device
	struct net_bridge_port *br_port;
	struct net_bridge_fdb_entry *br_fdb_sa;

	br_port = rcu_dereference(skb->dev->br_port);
	if( br_port != NULL && 
		(br_fdb_sa = br_fdb_get(br_port->br, &(skb->data[6]))) != NULL) {

		br_fdb_sa->ageing_timer = jiffies;
		skb->dev = br_port->br->dev;
		skb->input_dev = skb->dev;

		br_fdb_put(br_fdb_sa);
	}

#endif

	//if ((storlink_ctl.fast_net & 128) ||	// fast_net debug.
	if ((ipsec_tunnel[sw_id].mode == 1) && (ip_hdr->frag_off != 0))	// there is fragmentation
		return skb_send_to_kernel(skb);
	
	if ((ipsec_tunnel[sw_id].mode == 0) && (skb->len > 1430))	// when LAN packet is too big
		return skb_send_to_kernel(skb);

	result = ipsec_gmac_process(skb, sw_id);

	if (result == 0) {
		crypto_queue_count++;
	}

	// if crypto_queue are all in used!!! 
	if ((current_crypto_loc >= (current_crypto_rptr+CRYPTO_QUEUE_SIZE)) ||
			(crypto_queue_count == CRYPTO_QUEUE_SIZE)) {
//		printk("%s::reach crypto_queue's limit\n",__func__);
//		printk("%s::current_crypto_loc = %d, current_crypto_rptr = %d, ",__func__,
//			current_crypto_loc, current_crypto_rptr);
//		printk("crypto_queue_count = %d\n", crypto_queue_count);
//		crypto_queue_count = process_ipsec_batch(CRYPTO_QUEUE,crypto_queue_count,current_crypto_loc);
//		struct IPSEC_PACKET_S * crypto_ptr;
//		crypto_ptr = &(CRYPTO_QUEUE[current_crypto_rptr]);
//		if (crypto_ptr->used == 1) {
//			printk("%s::yes!! crypto queue is really full!!\n",__func__);
//		}
		ipsec_handle_skb_finish();
	}

	if (result !=0) {
		// need to clean skb here.. or it will cause error.
		// and have TCP or whatever protocol that runs in 
		// VPN tunnel to deal with missing/error packets
		if (skb->destructor)
		{
			// debug message
			//printk("%s::skb has destructor defined\n",__func__);
			dev_kfree_skb_any(skb);
		}
		else
			dev_kfree_skb(skb);
		skb = NULL;
	}

	return result;
}

/* ------------------------------------------------------------------------------
 * ipsec_handle_skb_finsh()
 * Description: With a successfully filled array of crypto engine packets, the 
 *				starting point of the crypto engine packet, and the number of 
 *				the packets, it calls an API from crypto engine driver, 
 *				process_ipsec_batch, which will handle the crypto engine packets
 *				and fill them in TX descriptor queue of crypto engine.  It also
 *				updates the number of remaing crypto engine packet in the queue.
 * -----------------------------------------------------------------------------*/
int ipsec_handle_skb_finish(void) {
	if (crypto_queue_count > 0)
	{
		if ((current_crypto_loc - crypto_queue_count) >= 0)
			crypto_queue_count = process_ipsec_batch(CRYPTO_QUEUE,crypto_queue_count,current_crypto_loc-crypto_queue_count);
		else
			crypto_queue_count = process_ipsec_batch(CRYPTO_QUEUE,crypto_queue_count,current_crypto_loc+CRYPTO_QUEUE_SIZE-crypto_queue_count);

		if (crypto_queue_count != 0) {
//			printk("%s::still got some entries remaining in the crypto queue\n",__func__);
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(ipsec_handle_skb_finish);

/*------------------------------------------------------------------------
 * ipsec_gmac_process()
 * Description: to handle packet that's being received by the class queue.
 *				In batch mode, it finds a free crypto engine packet slot 
 *				from an allocated crypto engine packet array and then complete 
 *				the crypto engine packet.
 *				In default 1-to-1 mode, it will create a new crypto engine
 *				packet, complete the crypto engine packet with necessary
 *				information, and sends it to crypto engine.
 *				Return 0, if succeeds.  And otherwise.
 *------------------------------------------------------------------------*/
int ipsec_gmac_process(struct sk_buff *skb, unsigned int sw_id)
{
    struct IPSEC_PACKET_S *crypto_ops;
    struct IPSEC_VPN_TUNNEL_CONFIG *ipsec_tunnel_ptr = NULL;
	struct iphdr *ip_hdr = NULL;
	struct ip_esp_hdr *esp_hdr;
	int result = 0;
	int ip_hdr_len;
	int i;
	
	ipsec_tunnel_ptr = &(ipsec_tunnel[sw_id]);

	if (ipsec_tunnel_ptr == NULL)
	{
		printk("%s::Connection doesn't belong in existing tunnel configurations\n",__func__);
		printk("src ip = %x, dst ip = %x\n", ntohl(ip_hdr->saddr), ntohl(ip_hdr->daddr));
		// free skb too !!!!
		// ipsec_handle_skb function will take care of skb free
//		if (skb->destructor)
//			dev_kfree_skb_any(skb);
//		else
//			dev_kfree_skb(skb);
		return -1;
	}

	if (ipsec_tunnel_ptr->enable == 0)
	{
		printk("%s::err, tunnel is not enabled\n",__func__);
		// ipsec_handle_skb function will take care of skb free
//		if (skb->destructor)
//			dev_kfree_skb_any(skb);
//		else
//			dev_kfree_skb(skb);
		return -1;
	}

#if 0	// debug message
	skb->nh.iph = (struct iphdr*)(&skb->data[14]);
	if ((((ntohl(skb->nh.iph->saddr) & ipsec_tunnel_ptr->src_netmask) != ipsec_tunnel_ptr->src_LAN)
			|| (ipsec_tunnel_ptr->dst_LAN != (ntohl(skb->nh.iph->daddr)& ipsec_tunnel_ptr->dst_netmask)))
			&& (ipsec_tunnel_ptr->mode == 0)) {
		printk("%s::error before going encryption\n",__func__);
		printk("src ip(%x) vs src LAN(%x), ", ntohl(skb->nh.iph->saddr), ipsec_tunnel_ptr->src_LAN);
		printk("dst ip(%x) vs dst LAN(%x)\n", ntohl(skb->nh.iph->daddr), ipsec_tunnel_ptr->dst_LAN);
	}
	if (((ntohl(skb->nh.iph->saddr) != ipsec_tunnel_ptr->src_WAN_IP)
			|| (ipsec_tunnel_ptr->dst_WAN_IP != ntohl(skb->nh.iph->daddr)))
			&& (ipsec_tunnel_ptr->mode == 1)) {
		printk("%s::error before going decryption\n",__func__);
		printk("src ip(%x) vs src LAN(%x), ", ntohl(skb->nh.iph->saddr), ipsec_tunnel_ptr->src_WAN_IP);
		printk("dst ip(%x) vs dst LAN(%x)\n", ntohl(skb->nh.iph->daddr), ipsec_tunnel_ptr->dst_WAN_IP);
	}
#endif

//	spin_lock_irqsave(&ipsec_ptr_handle_lock,flags);
	// allocate the ipsec packet for crypto engine
#ifdef CONFIG_CRYPTO_BATCH
	if (current_crypto_loc == CRYPTO_QUEUE_SIZE)
		current_crypto_loc = 0;
	crypto_ops = &(CRYPTO_QUEUE[current_crypto_loc]);
	if (crypto_ops->used == 1)
	{
//		printk("%s::crypto_packet at %d is in use\n",__func__,current_crypto_loc);
		// just simply drop the current skb.. it's the easiest way to save the effort
		// will think about other way to fix it later.
//		spin_unlock_irqrestore(&ipsec_ptr_handle_lock,flags);
		return 1;
	}
	memset(crypto_ops, 0, sizeof(struct IPSEC_PACKET_S));
#else
	crypto_ops = (struct IPSEC_PACKET_S*)kmalloc(sizeof(struct IPSEC_PACKET_S),GFP_ATOMIC);
	if (!crypto_ops)
	{
		// error checking.. it's just here for decoration
		kfree(crypto_ops);
//		spin_unlock_irqrestore(&ipsec_ptr_handle_lock,flags);
		return -1;
	}
#endif

	ip_hdr = (struct iphdr*)(skb->data+ENET_HEADER_SIZE);
		
	if (ipsec_tunnel_ptr->mode == 0)		// encryption
	{
		count_encrypted++;
		// debug message
//		printk("ipsec_gmac_process:: got here, going to encrypt the packet\n");
		read_lock(&ipsec_tunnel_lock);
		ip_hdr_len = ntohs(ip_hdr->tot_len);
		//crypto_ops->op_mode = ipsec_tunnel_ptr->op_mode;
		crypto_ops->op_mode = ENC_AUTH;
		crypto_ops->cipher_algorithm = ipsec_tunnel_ptr->cipher_alg;
		crypto_ops->auth_algorithm = ipsec_tunnel_ptr->auth_alg;
		crypto_ops->auth_result_mode = AUTH_APPEND;
		crypto_ops->iv_size = ipsec_tunnel_ptr->enc_iv_len;
		memcpy(crypto_ops->iv, ipsec_tunnel_ptr->enc_iv, ipsec_tunnel_ptr->enc_iv_len);
		//for (i=0; i<ipsec_tunnel_ptr->enc_iv_len;i++)
		//	crypto_ops->iv[i] = ipsec_tunnel_ptr->enc_iv[i];

		// insert IP and ESP header into the current skb
		skb->data = skb_push(skb,IP_HEADER_SIZE+sizeof(struct ip_esp_hdr)+ipsec_tunnel_ptr->enc_iv_len);
		memcpy(skb->data,skb->data+IP_HEADER_SIZE+sizeof(struct ip_esp_hdr)+ipsec_tunnel_ptr->enc_iv_len,34);

		esp_hdr = (struct ip_esp_hdr*)(skb->data+34);
		esp_hdr->spi = ipsec_tunnel_ptr->spi;
		esp_hdr->seq_no = htonl(++ipsec_tunnel_ptr->xfrm->replay.oseq);
		ipsec_tunnel_ptr->current_sequence = ipsec_tunnel_ptr->xfrm->replay.oseq;
		memcpy(skb->data+34+sizeof(struct ip_esp_hdr), ipsec_tunnel_ptr->enc_iv, ipsec_tunnel_ptr->enc_iv_len);

		// debug
//		i = memcmp(skb->data+34,esp_hdr,8);
//		if (i != 0)
//			printk("%s::skb->data+34 != esp_hdr\n",__func__);
		int clen = skb->len - ENET_HEADER_SIZE - IP_HEADER_SIZE - sizeof(struct ip_esp_hdr) - ipsec_tunnel_ptr->enc_iv_len;
		int old_skblen = clen;
		int alen, blksize;
		int nfrags;
		struct sk_buff *trailer;

		if ((ipsec_tunnel_ptr->auth_alg == 0) || (ipsec_tunnel_ptr->auth_alg == 2))
			alen = 20;
		else if ((ipsec_tunnel_ptr->auth_alg == 1) || (ipsec_tunnel_ptr->auth_alg == 3))
			alen = 16;
		else
			alen = 20;	// well shouldn't get here.
		if ((ipsec_tunnel_ptr->cipher_alg == 2) || (ipsec_tunnel_ptr->cipher_alg == 6))
			blksize = 16;
		else
			blksize = 8;
		blksize = ALIGN(blksize,4);
		clen = ALIGN(clen+2,blksize);
		if ((nfrags = skb_cow_data(skb,clen-old_skblen+alen,&trailer)) < 0)
			printk("%s::it shouldn't get here\n",__func__);
		do {
			for (i=0; i<clen-old_skblen - 2; i++)
				*(u8*)(trailer->tail + i) = i+1;
		} while (0);
		*(u8*)(trailer->tail + clen-old_skblen -2) = (clen - old_skblen)-2;
		pskb_put(skb,trailer,clen-old_skblen);
		*(u8*)(trailer->tail - 1) = IPPROTO_IPIP;
		crypto_ops->auth_header_len = ENET_HEADER_SIZE + IP_HEADER_SIZE;
		crypto_ops->auth_algorithm_len = clen + sizeof(struct ip_esp_hdr) + ipsec_tunnel_ptr->enc_iv_len;
		crypto_ops->cipher_header_len = ENET_HEADER_SIZE + IP_HEADER_SIZE + sizeof(struct ip_esp_hdr) + ipsec_tunnel_ptr->enc_iv_len;
		crypto_ops->cipher_algorithm_len = clen;

		ip_hdr = (struct iphdr *)(skb->data+ENET_HEADER_SIZE);
		ip_hdr->protocol = ipsec_tunnel_ptr->protocol;
		ip_hdr->frag_off = 0;
		read_unlock(&ipsec_tunnel_lock);
	}
	else if (ipsec_tunnel_ptr->mode == 1)		// decryption
	{
		count_decrypted++;
		// debug message
//		printk("ipsec_gmac_process:: got here, going to decrypt the packet\n");
		esp_hdr = (struct ip_esp_hdr*)(skb->data+34);

		read_lock(&ipsec_tunnel_lock);
		if (esp_hdr->seq_no != htonl(++ipsec_tunnel_ptr->current_sequence))
		{
			if (esp_hdr->seq_no > htonl(ipsec_tunnel_ptr->current_sequence)) {
				// lose 1 or more packets
//				printk("%s::case 1:... esp_hdr->seq_no = %d, ipsec_tunnel->curr_seq = %d\n",__func__,
//							ntohl(esp_hdr->seq_no), ipsec_tunnel_ptr->current_sequence);
				ipsec_tunnel_ptr->current_sequence = ntohl(esp_hdr->seq_no);
			}
			else {
				// hm. this shouldn't happen, sequence number drops back..
//				printk("%s::case 2:... esp_hdr->seq_no = %d, ipsec_tunnel->curr_seq = %d\n",__func__,
//							ntohl(esp_hdr->seq_no), ipsec_tunnel_ptr->current_sequence);
			}
		}
//		crypto_ops->op_mode = ipsec_tunnel_ptr->op_mode;
//		printk("%s::skb->len=%d,ip_hdr->tot_len=%d\n",__func__,skb->len,ntohs(ip_hdr->tot_len));
		ip_hdr_len = ntohs(ip_hdr->tot_len);
		crypto_ops->op_mode = AUTH_DEC;
		crypto_ops->cipher_algorithm = ipsec_tunnel_ptr->cipher_alg;
		crypto_ops->auth_algorithm = ipsec_tunnel_ptr->auth_alg;
		crypto_ops->auth_result_mode = AUTH_CHKVAL;
		crypto_ops->iv_size = ipsec_tunnel_ptr->enc_iv_len;
		memcpy(crypto_ops->iv, skb->data+42, ipsec_tunnel_ptr->enc_iv_len);
		crypto_ops->auth_header_len = ENET_HEADER_SIZE + IP_HEADER_SIZE;
		crypto_ops->cipher_header_len = ENET_HEADER_SIZE + IP_HEADER_SIZE + sizeof(struct ip_esp_hdr) + ipsec_tunnel_ptr->enc_iv_len;
		crypto_ops->auth_algorithm_len = ip_hdr_len - sizeof(struct iphdr) - ipsec_tunnel_ptr->icv_trunc_len;
		crypto_ops->cipher_algorithm_len = ip_hdr_len - sizeof(struct iphdr) - sizeof(struct ip_esp_hdr) -  ipsec_tunnel_ptr->enc_iv_len - ipsec_tunnel_ptr->icv_trunc_len;		// subtract IP header, ESP header and AH Trailer
		memcpy(crypto_ops->auth_checkval,skb->data+skb->len-ipsec_tunnel_ptr->icv_trunc_len, ipsec_tunnel_ptr->icv_trunc_len);		//?? check_val problem??
		read_unlock(&ipsec_tunnel_lock);
	}
	else
	{
		printk("%s::Something is wrong, packet shouldn't go to other class queue !\n",__func__);
		// ipsec_handle_skb function will take care of skb free
//		dev_kfree_skb_irq(skb);
//		spin_unlock_irqrestore(&ipsec_ptr_handle_lock,flags);
		return -1;
	}

	for (i=0; i<ipsec_tunnel_ptr->auth_key_len;i++)
		crypto_ops->auth_key[i] = ipsec_tunnel_ptr->auth_key[i];
	crypto_ops->auth_key_size = ipsec_tunnel_ptr->auth_key_len;
	for (i=0; i<ipsec_tunnel_ptr->enc_key_len;i++)
		crypto_ops->cipher_key[i] = ipsec_tunnel_ptr->enc_key[i];
	crypto_ops->cipher_key_size = ipsec_tunnel_ptr->enc_key_len;

	// create the input scatterlist and make the pointer of IPSEC_PACKET_S to it
#ifndef CONFIG_CRYPTO_BATCH
	input_ptr = (struct scatterlist *)kmalloc(sizeof(struct scatterlist),GFP_ATOMIC);
	input_ptr->page = virt_to_page((void*)skb->data);
	input_ptr->offset = offset_in_page((void*)skb->data);
	input_ptr->length = skb->len;
	crypto_ops->in_packet = input_ptr;
#endif
	crypto_ops->in_packet2 = (unsigned char*)skb->data;
	crypto_ops->input_skb = skb;

	crypto_ops->pkt_len = skb->len;
	crypto_ops->callback = ipsec_finish_callback;
	crypto_ops->used = 1;

#ifdef CONFIG_SL351X_IPSEC_REUSE_SKB
	// using new skb rather than resue the old one
	crypto_ops->output_skb = skb;
	crypto_ops->out_packet = (unsigned char*)(crypto_ops->output_skb->data);
#else
	// the output from crypto engine will be stored in a new skb
	// so later if the output is not regonized, malformed, we can
	// netif_rx the original packet
	crypto_ops->output_skb = alloc_skb(MAX_SKB_SIZE,GFP_ATOMIC);
	if (!crypto_ops->output_skb) {
		memset(crypto_ops, 0, sizeof(struct IPSEC_PACKET_S));
		return -1;
	}
	skb_reserve(crypto_ops->output_skb, skb->data - skb->head);
	crypto_ops->output_skb->dev = skb->dev;
	crypto_ops->output_skb->input_dev = skb->dev;
	crypto_ops->output_skb->protocol = skb->protocol;
	crypto_ops->out_packet = (unsigned char*)(crypto_ops->output_skb->data);
#endif

	crypto_ops->icv_full_len = ipsec_tunnel_ptr->icv_full_len;
	crypto_ops->icv_trunc_len = ipsec_tunnel_ptr->icv_trunc_len;
	crypto_ops->tunnel_ID = ipsec_tunnel_ptr->tableID;

//	spin_unlock_irqrestore(&ipsec_ptr_handle_lock,flags);

#ifdef CONFIG_CRYPTO_BATCH
	current_crypto_loc++;
	result = 0;
#else
	result = ipsec_crypto_hw_process(crypto_ops);
#endif

	if (result != 0)
		printk("%s::Something is wrong when calling ipsec_crypto_hw_process!\n",__func__);

	return result;
}

/*--------------------------------------------------------------*
 * ipsec_finish_callback
 * Description: for sl2312_ipsec module to callback the sl351x_ipsec,
 *				so it can send the processed information back and
 *				sl351x_ipsec module can process the encrypted/decrypted
 *				info to GMAC and send it out
 *------------------------------------------------------------*/
void ipsec_finish_callback(struct IPSEC_PACKET_S *ipsec_ptr)
{
	struct iphdr* ip_hdr;
	struct iphdr* ip_hdr2;
	struct ip_esp_hdr* esp_hdr;
	struct IPSEC_VPN_TUNNEL_CONFIG *ipsec_tunnel_ptr = NULL;
	struct sk_buff *skb;
	struct sk_buff *original_skb;
	u8 nexthdr[2];
	int padlen;
	int flag_polling;
	
	packet_error = 0;

//	if (ipsec_ptr == NULL)
//		printk("%s::NOOOO~~~\n",__func__);

	if (ipsec_ptr->status == 2) {
		packet_error = 1;
		goto end_finish_callback;
	}

//	spin_lock_irqsave(&ipsec_ptr_handle_lock,flags);

	skb = ipsec_ptr->output_skb;
#ifndef CONFIG_SL351X_IPSEC_REUSE_SKB
	skb->input_dev = ipsec_ptr->input_skb->input_dev;
#endif
	skb->pkt_type = PACKET_OTHERHOST;

	ipsec_tunnel_ptr = &(ipsec_tunnel[ipsec_ptr->tunnel_ID]);

	if (skb->data != ipsec_ptr->out_packet)
		printk("%s::...\n",__func__);

#if 0	// debug
	skb->nh.iph = (struct iphdr*)(&skb->data[14]);
	ipsec_ptr->input_skb->nh.iph = (struct iphdr*)(&ipsec_ptr->input_skb->data[14]);
	printk("%s::src ip = %x vx %x\n",__func__,ntohl(skb->nh.iph->saddr),ntohl(ipsec_ptr->input_skb->nh.iph->saddr));
	printk("dst ip = %x vs %x\n",ntohl(skb->nh.iph->daddr),ntohl(ipsec_ptr->input_skb->nh.iph->daddr));
	printk("id = %x vs %x\n",ntohl(skb->nh.iph->id),ntohl(ipsec_ptr->input_skb->nh.iph->id));
#endif

#if 1	// test of crypto'ed packets
	skb->nh.iph = (struct iphdr*)(&skb->data[14]);
	ipsec_ptr->input_skb->nh.iph = (struct iphdr*)(&ipsec_ptr->input_skb->data[14]);
	if (ipsec_ptr->used == 0)
		printk("%s::... this shouldn't... happen\n",__func__);

	if ((((ntohl(skb->nh.iph->saddr) & ipsec_tunnel_ptr->src_netmask) != ipsec_tunnel_ptr->src_LAN)
			|| (ipsec_tunnel_ptr->dst_LAN != (ntohl(skb->nh.iph->daddr)& ipsec_tunnel_ptr->dst_netmask)))
			&& (ipsec_tunnel_ptr->mode == 0)) {
		printk("%s::error right after finishing encryption\n",__func__);
#if 0		// debug
		printk("New Packet:\nIP:\nversion(%d) ihl(%d) tos(%d) tot_len(%d)\n",skb->nh.iph->version,
			skb->nh.iph->ihl,skb->nh.iph->tos,ntohs(skb->nh.iph->tot_len));
		printk("id(%d) frag(%x) ttl(%d) protocol(%d) check(%x)\n",skb->nh.iph->id,
			skb->nh.iph->frag_off,skb->nh.iph->ttl,skb->nh.iph->protocol,skb->nh.iph->check);
		printk("saddr(%x) daddr(%x)\n",ntohl(skb->nh.iph->saddr),ntohl(skb->nh.iph->daddr));
		esp_hdr = (struct ip_esp_hdr*)(skb->data+34);
		printk("ESP:\nspi(%x) seq_no(%d) iv(%x)\n\n",ntohl(esp_hdr->spi),esp_hdr->seq_no,(unsigned int)(esp_hdr+8));

		printk("Old Packet:\nIP:\nversion(%d) ihl(%d) tos(%d) tot_len(%d)\n",ipsec_ptr->input_skb->nh.iph->version,
			ipsec_ptr->input_skb->nh.iph->ihl,ipsec_ptr->input_skb->nh.iph->tos,ntohs(ipsec_ptr->input_skb->nh.iph->tot_len));
		printk("id(%d) frag(%x) ttl(%d) protocol(%d) check(%x)\n",ipsec_ptr->input_skb->nh.iph->id,
			ipsec_ptr->input_skb->nh.iph->frag_off,ipsec_ptr->input_skb->nh.iph->ttl,ipsec_ptr->input_skb->nh.iph->protocol,ipsec_ptr->input_skb->nh.iph->check);
		printk("saddr(%x) daddr(%x)\n",ntohl(ipsec_ptr->input_skb->nh.iph->saddr),ntohl(ipsec_ptr->input_skb->nh.iph->daddr));
		esp_hdr = (struct ip_esp_hdr*)(ipsec_ptr->input_skb->data+34);
		printk("ESP:\nspi(%x) seq_no(%d) iv(%x)\n",ntohl(esp_hdr->spi),esp_hdr->seq_no,(unsigned int)(esp_hdr+8));

		printk("Address:\nold skb(%x), skb->data(%x)\nnew skb(%x), new skb->data(%x)\n",&(ipsec_ptr->input_skb),
			&(ipsec_ptr->input_skb->data),&(ipsec_ptr->output_skb), &(ipsec_ptr->output_skb->data));

		printk("current_crypto_wptr = %d, current_crypto_rptr = %d\n\n\n",current_crypto_loc,current_crypto_rptr);
#endif
		printk("%s::packet will be handled by kernel\n",__func__);
		packet_error = 1;
		goto end_finish_callback;
	}

	if (((ntohl(skb->nh.iph->saddr) != ipsec_tunnel_ptr->src_WAN_IP)
			|| (ipsec_tunnel_ptr->dst_WAN_IP != ntohl(skb->nh.iph->daddr)))
			&& (ipsec_tunnel_ptr->mode == 1)) {
		printk("%s::error right after finishing decryption\n",__func__);
#if 0		// debug
		printk("New Packet:\nIP:\nversion(%d) ihl(%d) tos(%d) tot_len(%d)\n",skb->nh.iph->version,
			skb->nh.iph->ihl,skb->nh.iph->tos,ntohs(skb->nh.iph->tot_len));
		printk("id(%d) frag(%x) ttl(%d) protocol(%d) check(%x)\n",skb->nh.iph->id,
			skb->nh.iph->frag_off,skb->nh.iph->ttl,skb->nh.iph->protocol,skb->nh.iph->check);
		printk("saddr(%x) daddr(%x)\n",ntohl(skb->nh.iph->saddr),ntohl(skb->nh.iph->daddr));

		printk("Old Packet:\nIP:\nversion(%d) ihl(%d) tos(%d) tot_len(%d)\n",ipsec_ptr->input_skb->nh.iph->version,
			ipsec_ptr->input_skb->nh.iph->ihl,ipsec_ptr->input_skb->nh.iph->tos,ntohs(ipsec_ptr->input_skb->nh.iph->tot_len));
		printk("id(%d) frag(%x) ttl(%d) protocol(%d) check(%x)\n",ipsec_ptr->input_skb->nh.iph->id,
			ipsec_ptr->input_skb->nh.iph->frag_off,ipsec_ptr->input_skb->nh.iph->ttl,ipsec_ptr->input_skb->nh.iph->protocol,ipsec_ptr->input_skb->nh.iph->check);
		printk("saddr(%x) daddr(%x)\n",ntohl(ipsec_ptr->input_skb->nh.iph->saddr),ntohl(ipsec_ptr->input_skb->nh.iph->daddr));

//		printk("Address:\nold skb(%x), skb->data(%x)\nnew skb(%x), new skb->data(%x)\n",__func__,&(ipsec_ptr->input_skb),
//			&(ipsec_ptr->input_skb->data),&(ipsec_ptr->output_skb), &(ipsec_ptr->output_skb->data));
		printk("Address:\nold skb(%x), skb->data(%x)\nnew skb(%x), new skb->data(%x)\n\n",(__u32)ipsec_ptr->input_skb,
			(__u32)ipsec_ptr->input_skb->data,(__u32)ipsec_ptr->output_skb,(__u32)ipsec_ptr->output_skb->data);
		printk("current_crypto_wptr = %d, current_crypto_rptr = %d\n\n\n",current_crypto_loc,current_crypto_rptr);
#endif
		printk("%s::packet will be handled by kernel\n",__func__);
		packet_error = 1;
		goto end_finish_callback;
	}
#endif

	if (ipsec_ptr->op_mode == ENC_AUTH)
	{
		// skb management
		skb->tail = skb->data;
		skb->len = 0;
		skb_put(skb,ipsec_ptr->out_pkt_len - ipsec_ptr->icv_full_len + ipsec_ptr->icv_trunc_len);

		ip_hdr = (struct iphdr*)(skb->data + ENET_HEADER_SIZE);
//		skb_trim(skb,skb->len - ipsec_ptr->icv_full_len + ipsec_ptr->icv_trunc_len);
		ip_hdr->tot_len = htons(skb->len - ENET_HEADER_SIZE);
	}

	if (ipsec_ptr->op_mode == AUTH_DEC)
	{
		// skb management
		skb->tail = skb->data;
		skb->len = 0;
		// new direction
//		memmove(ipsec_ptr->out_packet+IP_HEADER_SIZE+16,ipsec_ptr->out_packet,ENET_HEADER_SIZE);
		skb->data = skb->data + IP_HEADER_SIZE + sizeof(struct ip_esp_hdr) + ipsec_tunnel_ptr->enc_iv_len;
		skb_put(skb,ipsec_ptr->out_pkt_len - IP_HEADER_SIZE - sizeof(struct ip_esp_hdr) - ipsec_tunnel_ptr->enc_iv_len);
		((struct ethhdr *)(skb->data))->h_proto = ((struct ethhdr *)(ipsec_ptr->out_packet))->h_proto;

		if (skb_copy_bits(skb, skb->len - ipsec_ptr->icv_trunc_len - 2, nexthdr, 2))
			BUG();
		padlen = nexthdr[0];

		skb_trim(skb, skb->len - ipsec_ptr->icv_trunc_len - padlen - 2);
		ip_hdr2 = (struct iphdr*)(skb->data + ENET_HEADER_SIZE);

		// newly added for authentication result 08/03/03
		// printk("%s::auth_cmp_result = %d\n",__func__,ipsec_ptr->auth_cmp_result);

//		printk("%s::protocol store in padding is %d\n",__func__,nexthdr[1]);
//		ip_hdr2->protocol = nexthdr[1];
//		printk("%s::nexthdr[0] = %d, nexthdr[1] = %d\n",__func__,nexthdr[0],nexthdr[1]);
	}

end_finish_callback:
#ifdef CONFIG_SL351X_IPSEC_REUSE_SKB
	original_skb = NULL;
#else
	original_skb = ipsec_ptr->input_skb;
#endif

#ifdef CONFIG_CRYPTO_BATCH
	flag_polling = ipsec_ptr->flag_polling;
//	memset(ipsec_ptr, 0, sizeof(struct IPSEC_PACKET_S));
	ipsec_ptr->used = 0;
	
	if ((flag_polling == 0) && (old_skb_queue_head != NULL)) {
		struct sk_buff * skb_ptr;
		while (old_skb_queue_head != NULL) {
			skb_ptr = old_skb_queue_head;
			old_skb_queue_head = old_skb_queue_head->next;
			skb_ptr->next = NULL;
			skb_send_to_kernel(skb_ptr);
		}
	}
#else
	kfree(ipsec_ptr->in_packet);
	kfree(ipsec_ptr);
#endif

	// update current_crypto_rptr
	current_crypto_rptr++;
	if (current_crypto_rptr == CRYPTO_QUEUE_SIZE)
		current_crypto_rptr = 0;
//	spin_unlock_irqrestore(&ipsec_ptr_handle_lock,flags);
	ipsec_gmac_callback(skb, ipsec_tunnel_ptr,original_skb,flag_polling);
}

/*-----------------------------------------------------------------*
 * ipsec_gmac_callback
 * Description: for sl351x_ipsec to fill up mac address for skb, call 
 *				xmit routine to send the skb out
 *----------------------------------------------------------------*/
int ipsec_gmac_callback(struct sk_buff *skb, struct IPSEC_VPN_TUNNEL_CONFIG * ipsec_tunnel_ptr, struct sk_buff *old_skb, int flag_polling)
{
	// create the ethernet header
	// send it out with hard_xmit method
	struct	iphdr *ip_hdr = (struct iphdr *)(skb->data+ENET_HEADER_SIZE);
	__u32	dip, sip;
	__u8	ip_tos;
	int		send_new_packet = 1;		// flag to control which packet is going to be sent
	u8 nexthdr[2];
	int padlen;
	int in_interface;
	struct net_bridge_fdb_entry *br_fdb, *br_fdb_sa;
	struct net_bridge *br;
	struct dst_entry *dst;
	int cpu;
	int hh_alen;
	struct hh_cache *hh;

//	printk("%s::got here!!\n",__func__);
	
#if 0	// redudant check
	if (ipsec_tunnel_ptr == NULL) {
		printk("%s::Connection doesn't belong in existing tunnel configurations\n",__func__);
		printk("src ip = %x, dst ip = %x\n", ntohl(ip_hdr->saddr), ntohl(ip_hdr->daddr));
		// free skb too !!!!
		if (skb->destructor)
			dev_kfree_skb_any(skb);
		else
			dev_kfree_skb(skb);
		return -1;
	}

	if (skb == NULL) {
		printk("%s::skb shouldn't be NULL!!\n",__func__);
		return -1;
	}

	if (skb->dev == NULL) {
		printk("%s::shouldn't get here. no device is available to send\n",__func__);
		// weird bypass (because skb->input_dev should've been set up
		skb->dev = skb->input_dev;
	}

	if (ipsec_tunnel_ptr->enable == 0) {
		printk("%s::err, tunnel is not enabled\n",__func__);
		if (skb->destructor)
			dev_kfree_skb_any(skb);
		else
			dev_kfree_skb(skb);
		return -1;
	}

	if (skb->data == NULL) {
		printk("%s::err, skb->data == NULL\n",__func__);
		if (skb->destructor)
			dev_kfree_skb_any(skb);
		else
			dev_kfree_skb(skb);
		return -1;
	}
#endif

	skb->mac.raw = skb->data;
	skb->nh.iph = (struct iphdr*)(&skb->data[ENET_HEADER_SIZE]);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (ipsec_tunnel_ptr->mode == 0)		// encryption
	{
#if 0
		// debugging purpose!!
		if (((ntohl(skb->nh.iph->saddr) & ipsec_tunnel_ptr->src_netmask) != ipsec_tunnel_ptr->src_LAN)
			|| (ipsec_tunnel_ptr->dst_LAN != (ntohl(skb->nh.iph->daddr)& ipsec_tunnel_ptr->dst_netmask))) {
			printk("%s::error, the ip info after encryption is bugged\n",__func__);
			printk("src ip(%x) vs src LAN(%x), ", ntohl(skb->nh.iph->saddr), ipsec_tunnel_ptr->src_LAN);
			printk("dst ip(%x) vs dst LAN(%x)\n", ntohl(skb->nh.iph->daddr), ipsec_tunnel_ptr->dst_LAN);
			if (skb->destructor)
				dev_kfree_skb_any(skb);
			else
				dev_kfree_skb(skb);
			return -1;
		}
#endif
		skb->nh.iph->saddr = htonl(ipsec_tunnel_ptr->src_WAN_IP);
		skb->nh.iph->daddr = htonl(ipsec_tunnel_ptr->dst_WAN_IP);
	}

	// a packet that's sent to the router itself
	// we should send it to kernel to handle it
	// send the original packet to kernel!
	if ((skb!=NULL) && (ip_hdr->daddr == htonl(local_IP)))
	{
#ifdef CONFIG_SL351X_IPSEC_REUSE_SKB
		struct  ethhdr *eth = (struct ethhdr *)(skb->data);
		memcpy(eth->h_dest,skb->dev->dev_addr,skb->dev->addr_len);
		skb->pkt_type = PACKET_HOST;
		return skb_send_to_kernel(skb);
#else
		send_new_packet = 0;
		goto send_packet;
#endif
	}

	if (packet_error == 1) {
		send_new_packet = 0;
		goto send_packet;
	}

	dip = skb->nh.iph->daddr;
	sip = skb->nh.iph->saddr;
	ip_tos = skb->nh.iph->tos;

#if 0
	// debugging purpose...
	if ((ipsec_tunnel_ptr->mode == 1) && 
			((ipsec_tunnel_ptr->src_LAN != (ntohl(sip) & ipsec_tunnel_ptr->src_netmask)) 
			|| (ipsec_tunnel_ptr->dst_LAN != (ntohl(dip) & ipsec_tunnel_ptr->dst_netmask)))) {
		printk("%s::error, the ip info after decryption is bugged\n",__func__);
		printk("src ip(%x) vs src LAN(%x), ", ntohl(sip), ipsec_tunnel_ptr->src_LAN);
		printk("dst ip(%x) vs dst LAN(%x)\n", ntohl(dip), ipsec_tunnel_ptr->dst_LAN);
//		if ((ipsec_tunnel_ptr->src_LAN != (ntohl(sip) & ipsec_tunnel_ptr->src_netmask)) 
//			|| (ipsec_tunnel_ptr->dst_LAN != (ntohl(dip) & ipsec_tunnel_ptr->dst_netmask)))
//			printk("%x\n",skb->data);
		if (skb->destructor)
			dev_kfree_skb_any(skb);
		else
			dev_kfree_skb(skb);
		return -1;
	}
#endif

	// process of finding destination (or gateway)'s Mac Address
	// 1. find if the routing cache exists or not, if so, use it
	// 2. else, find the routing table
	// 3. if rtable is found, use gateway to look up for arp table
	// 4. else use destination IP.

	if(ipsec_tunnel_ptr->mode == 1) {
		// decryption
		in_interface = skb->dev->ifindex;
	}
	else {
		// encryption
		in_interface = 0;
	}
	
	if (sl_ip_route_cache(skb, dip, sip, ip_tos, skb->dev, in_interface, 0)) {
			
		skb->dev = skb->dst->dev;
		dst = (struct dst_entry*)skb->dst;
		hh = dst->hh;
		
		// no need to check for existence of hh, new sl_ip_route_cache only return true if hh exists.
		read_lock_bh(&hh->hh_lock);
		hh_alen = HH_DATA_ALIGN(hh->hh_len);
		memcpy(skb->data - 2, hh->hh_data, hh_alen);
		read_unlock_bh(&hh->hh_lock);
		//printk("sent throught fast routing cache!\n");
	}
	else
	{
#ifdef CONFIG_SL351X_IPSEC_REUSE_SKB
		struct in_device *ip_devptr;
        struct neigh_table *arp_table;
        struct  neighbour *n;
		struct  ethhdr *eth;

		// debug message
//		printk("%s:skb's sip = %x, dip = %x, tos = %x, ",__func__,sip,dip,tos);
//		printk("iif = %x, oif = %x, proto = %x\n",skb->input_dev->ifindex,skb->dev->ifindex,skb->nh.iph->protocol);

		ip_devptr = (struct in_device *)skb->dev->ip_ptr;
		arp_table = (struct neigh_table*)ip_devptr->arp_parms->tbl;

		// check the routing table
		struct rtable *rt;
		struct flowi fl = { .iif = skb->input_dev->ifindex,
							.oif = skb->dev->ifindex,
							.nl_u = { .ip4_u = { .daddr = dip,
												 .saddr = sip,
												 .tos = skb->nh.iph->tos }},
							.proto = skb->nh.iph->protocol,
//							.uli_u = { .ports = { .sport = sport,
//												  .dport = dport }}
		};
		int err = __ip_route_output_key(&rt,&fl);

//		printk("%s::err = %d\n",__func__,err);
		if (err == 0) {
			if (rt == NULL) {
				printk("%s::rt == NULL T__T\n",__func__);
				// clean skb? shouldn't happen right?
			}
//			printk("%s::src ip = %x, dst ip = %x, gw ip = %x\n",__func__,rt->rt_src,
//								rt->rt_dst,rt->rt_gateway);
//			printk("rt_iif = %x\n",rt->rt_iif);
			skb->dst = (struct dst_entry*)rt;
			dip = rt->rt_gateway;
		}
//		else
//		{
//			printk("%s::didn't find any routing entry, send as usual packet\n",__func__);
//			skb->protocol = eth_type_trans(skb,skb->dev);	/* set skb protocol */
//			return NF_HOOK(PF_INET,NF_IP_PRE_ROUTING, skb, NULL, skb->input_dev, dst_output);
//		}
		memcpy(eth->h_source,skb->dev->dev_addr,skb->dev->addr_len);

		// create cases for dual core or single core.
		// since 2nd CPU will have different MAC table,
		// we either have to have 1st CPU to store the MAC table in a shared
		// memory such that 2nd CPU can access it as well.
		// If that's the case, we will have to modify kernel's ARP table
		// maintaining code.
		
		// arp table look up method
		n = neigh_lookup(arp_table,&dip,skb->dev);
		if (n) {
			// update the timer
			n->used = jiffies;
//			printk("%s::%d mac address for %x is %02x.%02x.%02x.%02x.%02x.%02x",__func__,n->nud_state,ntohl(dip),
//				eth->h_dest[0],	eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]);
			if (n->nud_state&NUD_VALID) {
				read_lock_bh(&n->lock);
				memcpy(eth->h_dest,n->ha,skb->dev->addr_len);
				read_unlock_bh(&n->lock);
			}
			else
			{
				printk("%s::got here.. find the neighbor, but n is not valid\n",__func__);
				printk("%s::dip = %x, sip = %x\n",__func__,dip,sip);
				n->nud_state = NUD_VALID;
				read_lock_bh(&n->lock);
				memcpy(eth->h_dest,n->ha,skb->dev->addr_len);
				read_unlock_bh(&n->lock);
//				goto renew_neighbor;
			}
//			neigh_release(n);
		}
		else {
			// if not found, create an empty neigh structure, and have kernel fill it up
//			printk("%s::not able to find the mac address for dest:%x\n",__func__,ntohl(dip));
			skb->next = skb;
			skb->prev = skb;
			n = neigh_create(arp_table,&dip,skb->dev);
			struct sk_buff *aq_ptr = (struct sk_buff*)&n->arp_queue;
			aq_ptr->next = aq_ptr;
			aq_ptr->prev = aq_ptr;
//			printk("%s::n = %x\n",__func__,n);
//			printk("%s::n->arp_queue = %x\n",__func__,&(n->arp_queue));

//renew_neighbor:
			// debug message
			if (skb_shared(skb))
				printk("there are %d users refer to this buffer\n",skb->users);

			if (!neigh_event_send(n, skb_clone(skb,GFP_ATOMIC))) {
				int err;

				read_lock_bh(&n->lock);
				err = skb->dev->hard_header(skb,skb->dev,ntohs(skb->protocol),n->ha,NULL,skb->len);
				read_unlock_bh(&n->lock);
			
				if (err >= 0)
					n->ops->queue_xmit(skb);
				else
					printk(">_>\n");
				return 0;
			}
//			printk("%s::skb's new dst mac address is %02x.%02x.%02x.%02x.%02x.%02x\n",__func__,
//					n->ha[0],n->ha[1],n->ha[2],n->ha[3],n->ha[4],n->ha[5]);
//			neigh_release(n);
		}
#else
		send_new_packet = 0;
#endif
	}

/*
	if (skb->nh.iph->frag_off != 0)
	{
		printk("%s::fragmented packet after decryption\n",__func__);
		send_new_packet = 0;
	}
*/

	// handle the resulted skb.
	// if send_new_packet flag is on, the encrypted/decrypted packet will be sent out
	// if not, then original packet will be sent
	// to kernel and has kernel to handle it.
#ifdef CONFIG_SL351X_IPSEC_WITH_WIFI
	// check to see if destination device is a bridge device. send packet directly to the real device.
	if ((send_new_packet == 1) && (skb->dev->hard_start_xmit == br_dev_xmit)) {
		br = netdev_priv(skb->dev);
		if ( (br_fdb = br_fdb_get(br, &(skb->data[0]) )) != NULL ) {
				skb->dev = br_fdb->dst->dev;
				br_fdb_put(br_fdb);
		}
		else {
			struct  ethhdr *eth1 = (struct ethhdr *)(skb->data);
			printk("%s::fail to obtain the right device within the bridge device\n",__func__);
			printk("\tgoing to send the drop this hardware-accelerated packet for this time\n");
			printk("dst mac(%x), dst ip(%x), src mac(%x), src ip(%x)\n",
				eth1->h_dest,dip,eth1->h_source,sip);
			send_new_packet = 0;
		}
	}
#endif

	// when mtu is smaller than skb size. fragmentation might
	// involve in this case. have kernel deals with this packet
	if ((send_new_packet == 1) && ((skb->dev->mtu+16) < skb->len))
	{
//		printk("%s::resulted packet(%d) is bigger than MTU(%d)\n",__func__,skb->len,skb->dev->mtu);
//		printk("tunnel %d, skb->dev %d\n",ipsec_tunnel_ptr->mode,((GMAC_INFO_T *)skb->dev->priv)->port_id);
		send_new_packet = 0;
	}

send_packet:
	if (send_new_packet == 1)
	{
//		printk("%s::sending new packet\n",__func__);
		// clean old skb
		if (old_skb != NULL)
		{
			if (old_skb->destructor)
				dev_kfree_skb_any(old_skb);
			else
				dev_kfree_skb(old_skb);
		}

		// done adding the mac address for destination.
		// sending it out
		// should use the following but the performace is slower!!!
		skb->ip_summed = CHECKSUM_UNNECESSARY;
#if 0
		dev_queue_xmit(skb);
#endif
#if 1
		local_bh_disable();
		cpu = smp_processor_id();
		if (skb->dev->xmit_lock_owner != cpu) {
			spin_lock(&skb->dev->xmit_lock);
			skb->dev->xmit_lock_owner = cpu;

			if (netif_queue_stopped(skb->dev)) {
				printk("%s::Error!! netif_queue_stops! Drop the packet\n",__func__);
				skb->dev->xmit_lock_owner = -1;
				spin_unlock(&skb->dev->xmit_lock);
				local_bh_enable();
				if (skb != NULL)
				{
					if (skb->destructor)
						dev_kfree_skb_any(skb);
					else
						dev_kfree_skb(skb);
				}
				return -1;
			}

			if (skb->dev->hard_start_xmit(skb,skb->dev)) {
				printk("%s::hard_start_xmit fails! Drop the packet\n",__func__);
				skb->dev->xmit_lock_owner = -1;
				spin_unlock(&skb->dev->xmit_lock);
				local_bh_enable();
				if (skb != NULL)
				{
					if (skb->destructor)
						dev_kfree_skb_any(skb);
					else
						dev_kfree_skb(skb);
				}
				return -1;
			}
			skb->dev->xmit_lock_owner = -1;
			spin_unlock(&skb->dev->xmit_lock);
		}
		else
			if (net_ratelimit())
				printk(KERN_CRIT "%s::Dead loop on virtual device %s, fix it urgently1\n",
					__func__,skb->dev->name);
		local_bh_enable();
#endif
	}
	if (send_new_packet == 0)
	{
//		printk("%s::this packet is going software vpn path\n",__func__);
		// clean the new skb
		if (skb != NULL)
		{
			if (skb->destructor)
				dev_kfree_skb_any(skb);
			else
				dev_kfree_skb(skb);
		}

		// updat the old skb in the case for encryption path, because
		// original skb has been modified.
		if (ipsec_tunnel_ptr->mode == 0)		// encryption
		{
			memcpy(old_skb->data+IP_HEADER_SIZE+sizeof(struct ip_esp_hdr)+ipsec_tunnel_ptr->enc_iv_len,old_skb->data,14);
			old_skb->data = skb_pull(old_skb,IP_HEADER_SIZE+sizeof(struct ip_esp_hdr)+ipsec_tunnel_ptr->enc_iv_len);
			if (skb_copy_bits(skb, skb->len - ipsec_tunnel_ptr->icv_trunc_len -2, nexthdr, 2))
				BUG();
			padlen = nexthdr[0];
			skb_trim(skb, skb->len - padlen - 2);
			(ipsec_tunnel_ptr->xfrm->replay.oseq)--;
		}
//		printk("skb->len %d\n", old_skb->len);
//		ip_hdr = (struct iphdr *)(old_skb->data+ENET_HEADER_SIZE);
//		printk("%s::saddr = %x, daddr = %x\n",__func__,ip_hdr->saddr,ip_hdr->daddr);

#ifndef CONFIG_SL351X_IPSEC_REUSE_SKB
		if (flag_polling) 
		{
#if 0
			// crypto engine is in polling mode, better drop this packet
			if (old_skb != NULL)
			{
				if (old_skb->destructor)
					dev_kfree_skb_any(old_skb);
				else
					dev_kfree_skb(old_skb);
			}
#endif
			if (old_skb_queue_head == NULL) {
				old_skb_queue_head = old_skb;
				old_skb->next = NULL;
			}
			else {
				struct sk_buff * skb_ptr = old_skb_queue_head;
				while (skb_ptr->next != NULL)
					skb_ptr = skb_ptr->next;
				skb_ptr->next = old_skb;
				old_skb->next = NULL;
			}
		}
		else
		{
			skb_send_to_kernel(old_skb);
		}
#endif
	}
	
	return 0;
}

/* ----------------------------------------------------------------*
 * vpn_sysctl_info
 * Description: ioctl handler which is triggered when 
 *				/proc/sys/net/vpn/vpn_pair is changed.
 *				It first reads in the new values, and then updates
 *				tunnel configuration and reset all the connections.
 * ----------------------------------------------------------------*/
static int vpn_sysctl_info(ctl_table *ctl, int write, struct file * filp,
                           void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct IPSEC_VPN_IP_PAIR_CONFIG * pair_ptr;
	struct IPSEC_VPN_TUNNEL_CONFIG * tunnel_ptr;
	int ret, qid;
	int i, j, flag;
	int hash_entry_index = -1;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	// update the field of pair info when there is a write action on
	// /proc/sys/net/vpn/vpn_pair
	if (write) {
		memset(ipsec_pair, 0, MAX_IPSEC_TUNNEL*sizeof(struct IPSEC_VPN_IP_PAIR_CONFIG));
		for (i=0 ; i < MAX_IPSEC_TUNNEL ; i++)
		{
			ipsec_pair[i].enable = (__u8)vpn_info[i*9+0];
			ipsec_pair[i].direction = (__u8)vpn_info[i*9+1];
			ipsec_pair[i].src_LAN = (__u32)vpn_info[i*9+2];
			ipsec_pair[i].src_netmask = (__u32)vpn_info[i*9+3];
			ipsec_pair[i].src_LAN_GW = (__u32)vpn_info[i*9+4];
			ipsec_pair[i].dst_LAN = (__u32)vpn_info[i*9+5];
			ipsec_pair[i].dst_netmask = (__u32)vpn_info[i*9+6];
			ipsec_pair[i].src_WAN_IP = (__u32)vpn_info[i*9+7];
			ipsec_pair[i].dst_WAN_IP = (__u32)vpn_info[i*9+8];
			if (ipsec_pair[i].direction == 0)
			{
				if (local_IP == 0)
					local_IP = ipsec_pair[i].src_LAN_GW;
				if ((local_IP != ipsec_pair[i].src_LAN_GW) && (ipsec_pair[i].src_LAN_GW != 0))
					printk("%s::Source LAN Gateways of 1 or more Tunnel Setup do not match\n",__func__);					
				
				/* Maybe use this fucntion to make sure the local IP is correct..
				 * Confirm that local IP address exists using wildcards:
				 * - dev: only on this interface, 0=any interface
				 * - dst: only in the same subnet as dst, 0=any dst
				 * - local: address, 0=autoselect the local address
				 * - scope: maximum allowed scope value for the local address
				 */
				//u32 inet_confirm_addr(const struct net_device *dev, u32 dst, u32 local, int scope)
			}
		}
		// debug message
#if 0
		for (i=0 ; i < MAX_IPSEC_TUNNEL ; i++)
		{
			printk("#%d:%d %d %x %x %x %x %x %x %x\n", i, ipsec_pair[i].enable, 
				ipsec_pair[i].direction,ipsec_pair[i].src_LAN, 
				ipsec_pair[i].src_netmask, ipsec_pair[i].src_LAN_GW,
				ipsec_pair[i].dst_LAN, ipsec_pair[i].dst_netmask,
				ipsec_pair[i].src_WAN_IP, ipsec_pair[i].dst_WAN_IP);
		}
#endif
		// update tunnel
		pair_ptr = ipsec_pair;
		tunnel_ptr = ipsec_tunnel;
		i = 0;
		while ((tunnel_ptr != NULL) && (i < MAX_IPSEC_TUNNEL))
		{
			j = 0;
			flag = 0;
			while ((pair_ptr != NULL) && (j < MAX_IPSEC_TUNNEL))
			{
				// find the matching pair to this current tunnel.
				// so we change it's status to the new status that's been updated
				if ((pair_ptr->src_WAN_IP == tunnel_ptr->src_WAN_IP) &&
					(pair_ptr->dst_WAN_IP == tunnel_ptr->dst_WAN_IP) &&
					(pair_ptr->src_WAN_IP != 0) &&
					(pair_ptr->dst_WAN_IP != 0))
				{
					tunnel_ptr->tableID = i;
					tunnel_ptr->enable = pair_ptr->enable;
					tunnel_ptr->connection_count = 0;
					tunnel_ptr->src_LAN = pair_ptr->src_LAN;
					tunnel_ptr->src_netmask = pair_ptr->src_netmask;
					tunnel_ptr->src_LAN_GW = pair_ptr->src_LAN_GW;
					tunnel_ptr->dst_LAN = pair_ptr->dst_LAN;
					tunnel_ptr->dst_netmask = pair_ptr->dst_netmask;
					tunnel_ptr->mode = pair_ptr->direction;
					flag = 1;
					if (tunnel_ptr->sa_hash_flag == 0)
					{
						if (tunnel_ptr->mode == 0)
						{
							qid = IPSEC_TEST_OUTBOUND_QID;
							tunnel_ptr->protocol = IPPROTO_ESP;
						}
						else
							qid = IPSEC_TEST_INBOUND_QID;
						hash_entry_index = create_ipsec_hash_entry(tunnel_ptr, qid);
						if (hash_entry_index <0) {
							printk("%s::release classification queue hash entry %d!\n", __func__, qid);
						return 0;
						}
						printk("%s::class qid %d, hash_entry_index %x\n", __func__, qid, hash_entry_index);
						tunnel_ptr->sa_hash_entry = (__u16)hash_entry_index;
						hash_set_valid_flag(hash_entry_index, 1);
						tunnel_ptr->sa_hash_flag = 1;
					}
					hash_set_valid_flag(tunnel_ptr->sa_hash_entry, tunnel_ptr->enable);
				}
				pair_ptr++;
				j++;
			}
			// can't find the pair info from the existing tunnel set
			// means, the pair has been deleted.
			// clean the tunnel
			if (flag == 0)
			{
				hash_set_valid_flag(tunnel_ptr->sa_hash_entry, 0);
				hash_invalidate_entry(tunnel_ptr->sa_hash_entry);
				memset(tunnel_ptr, 0, sizeof(struct IPSEC_VPN_TUNNEL_CONFIG));
			}
			tunnel_ptr++;
			i++;
		}

		// reset connection, because the pair has been reseted.
//		memset(ipsec_conn, 0, MAX_IPSEC_CONN*sizeof(struct IPSEC_CONN_T));
	}
	return ret;
}

/*----------------------------------------------------------------
 *           IP2,hw_mac1           IP4, dst_mac2
 *            |                      |
 *      +-----+-RG1-+----------------+-RG2-+---------+
 *      |           |                      |         |
 *      |        IP3, hw_mac2             IP5        |
 *      |                                            |
 *     PC1                                          PC2
 *	IP1, dst_mac1                                   IP6
 *------------------------------------------------------*/

MODULE_AUTHOR("Wen Hsu<wen_hsu@stormsemi.com>");
MODULE_DESCRIPTION("Storm hardware-accelerated IPSEC-VPN");
