/*
 * Cryptographic API.
 *
 * Digest operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"

#ifdef CONFIG_SL2312_IPSEC
#include <linux/dma-mapping.h>
#include <asm/arch/sl2312_ipsec.h>

#define     IPSEC_TEXT_LEN    2048

DECLARE_WAIT_QUEUE_HEAD(auth_queue);
static unsigned int auth_done;
static struct IPSEC_PACKET_S auth_op;

static void (auth_callback)(struct IPSEC_PACKET_S *op_info)
{
	auth_done = 1;
	wake_up(&auth_queue);
}
#endif

static void init(struct crypto_tfm *tfm)
{
	tfm->__crt_alg->cra_digest.dia_init(crypto_tfm_ctx(tfm));
}

static void update(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg)
{
	unsigned int i;

#ifdef CONFIG_SL2312_IPSEC

	unsigned int plen=0;//,paddr;
	

		for(i=0;i<nsg;i++)
		{
			
			plen += sg[i].length;
		
		}
		
		memset(&auth_op,0x00,sizeof(struct IPSEC_PACKET_S));   
		auth_op.op_mode = AUTH;
		auth_op.auth_algorithm = ipsec_get_auth_algorithm((unsigned char *)tfm->__crt_alg->cra_name,0); //(0) AUTH; (1) HMAC
		auth_op.callback = auth_callback;
		auth_op.auth_header_len = 0;
		auth_op.in_packet = kmap(sg->page) + sg->offset;
		if(nsg>1)
			sg->length = plen;
		auth_op.pkt_len = sg->length;
		auth_op.auth_algorithm_len = sg->length;

#else
	for (i = 0; i < nsg; i++) {

		struct page *pg = sg[i].page;
		unsigned int offset = sg[i].offset;
		unsigned int l = sg[i].length;

		do {
			unsigned int bytes_from_page = min(l, ((unsigned int)
							   (PAGE_SIZE)) - 
							   offset);
			char *p = crypto_kmap(pg, 0) + offset;

			tfm->__crt_alg->cra_digest.dia_update
					(crypto_tfm_ctx(tfm), p,
					 bytes_from_page);
			crypto_kunmap(p, 0);
			crypto_yield(tfm);
			offset = 0;
			pg++;
			l -= bytes_from_page;
		} while (l > 0);
	}
#endif
}

static void
hexdump(unsigned char *buf, unsigned int len)
{
	while (len--)
		printk("%02x", *buf++);

	printk("\n");
}

static void final(struct crypto_tfm *tfm, u8 *out)
{
#ifdef CONFIG_SL2312_IPSEC
	int i;
	 
	wait_queue_t wait;
		
	init_waitqueue_entry(&wait, current);
	auth_op.out_packet = kmalloc(IPSEC_TEXT_LEN, GFP_DMA);//GFP_ATOMIC);//tmp;
	auth_done = 0;
	ipsec_crypto_hw_process(&auth_op);	
	add_wait_queue(&auth_queue, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (auth_done) /* whatever test your driver needs */
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&auth_queue, &wait);
	consistent_sync(auth_op.out_packet, IPSEC_TEXT_LEN, DMA_BIDIRECTIONAL);//PCI_DMA_FROMDEVICE); 
	memcpy(out, (u8 *)(auth_op.out_packet+auth_op.pkt_len),crypto_tfm_alg_digestsize(tfm));
	kfree(auth_op.out_packet);
    
#else  	
	tfm->__crt_alg->cra_digest.dia_final(crypto_tfm_ctx(tfm), out);
#endif
}

static int setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
#ifdef CONFIG_SL2312_IPSEC
    {
        auth_op.auth_key_size = keylen;
        memcpy(auth_op.auth_key,key,keylen);
        return 0;
    } 
#else	
	u32 flags;
	if (tfm->__crt_alg->cra_digest.dia_setkey == NULL)
		return -ENOSYS;
	return tfm->__crt_alg->cra_digest.dia_setkey(crypto_tfm_ctx(tfm),
						     key, keylen, &flags);
#endif
}

static void digest(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	unsigned int i;

	tfm->crt_digest.dit_init(tfm);
#ifdef CONFIG_SL2312_IPSEC
	update(tfm,sg,nsg);
	final(tfm,out);
#else		
		
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page, 0) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(crypto_tfm_ctx(tfm),
		                                      p, sg[i].length);
		crypto_kunmap(p, 0);
		crypto_yield(tfm);
	}
	crypto_digest_final(tfm, out);
#endif
}

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags)
{
	return flags ? -EINVAL : 0;
}

int crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct digest_tfm *ops = &tfm->crt_digest;
	
	ops->dit_init	= init;
	ops->dit_update	= update;
	ops->dit_final	= final;
	ops->dit_digest	= digest;
	ops->dit_setkey	= setkey;
	
	return crypto_alloc_hmac_block(tfm);
}

void crypto_exit_digest_ops(struct crypto_tfm *tfm)
{
	crypto_free_hmac_block(tfm);
}
