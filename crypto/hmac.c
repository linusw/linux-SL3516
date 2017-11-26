/*
 * Cryptographic API.
 *
 * HMAC: Keyed-Hashing for Message Authentication (RFC2104).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * The HMAC implementation is derived from USAGI.
 * Copyright (c) 2002 Kazunori Miyazawa <miyazawa@linux-ipv6.org> / USAGI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include "internal.h"

#ifdef CONFIG_SL2312_IPSEC

#include <asm/arch/sl2312_ipsec.h>

#define     IPSEC_TEXT_LEN    2048 + 256

DECLARE_WAIT_QUEUE_HEAD(auth_hmac_queue);
static unsigned int auth_hmac_done;
static struct IPSEC_PACKET_S auth_hmac_op;

static void (auth_hmac_callback)(struct IPSEC_PACKET_S *op_info)
{
	auth_hmac_done = 1;
	wake_up(&auth_hmac_queue);
}

#endif

static void hash_key(struct crypto_tfm *tfm, u8 *key, unsigned int keylen)
{
	struct scatterlist tmp;
	
	sg_set_buf(&tmp, key, keylen);
	crypto_digest_digest(tfm, &tmp, 1, key);
}

int crypto_alloc_hmac_block(struct crypto_tfm *tfm)
{
	int ret = 0;

	BUG_ON(!crypto_tfm_alg_blocksize(tfm));
	
	tfm->crt_digest.dit_hmac_block = kmalloc(crypto_tfm_alg_blocksize(tfm),
	                                         GFP_KERNEL);
	if (tfm->crt_digest.dit_hmac_block == NULL)
		ret = -ENOMEM;

	return ret;
		
}

void crypto_free_hmac_block(struct crypto_tfm *tfm)
{
	kfree(tfm->crt_digest.dit_hmac_block);
}

void crypto_hmac_init(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen)
{
	unsigned int i;
	struct scatterlist tmp;
	char *ipad = tfm->crt_digest.dit_hmac_block;
	
	if (*keylen > crypto_tfm_alg_blocksize(tfm)) {
		hash_key(tfm, key, *keylen);
		*keylen = crypto_tfm_alg_digestsize(tfm);
	}

	memset(ipad, 0, crypto_tfm_alg_blocksize(tfm));
	memcpy(ipad, key, *keylen);

	for (i = 0; i < crypto_tfm_alg_blocksize(tfm); i++)
		ipad[i] ^= 0x36;

	sg_set_buf(&tmp, ipad, crypto_tfm_alg_blocksize(tfm));
	
	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
}

void crypto_hmac_update(struct crypto_tfm *tfm,
                        struct scatterlist *sg, unsigned int nsg)
{
	crypto_digest_update(tfm, sg, nsg);
}

void crypto_hmac_final(struct crypto_tfm *tfm, u8 *key,
                       unsigned int *keylen, u8 *out)
{
	unsigned int i;
	struct scatterlist tmp;
	char *opad = tfm->crt_digest.dit_hmac_block;
	
	if (*keylen > crypto_tfm_alg_blocksize(tfm)) {
		hash_key(tfm, key, *keylen);
		*keylen = crypto_tfm_alg_digestsize(tfm);
	}

	crypto_digest_final(tfm, out);

	memset(opad, 0, crypto_tfm_alg_blocksize(tfm));
	memcpy(opad, key, *keylen);
		
	for (i = 0; i < crypto_tfm_alg_blocksize(tfm); i++)
		opad[i] ^= 0x5c;

	sg_set_buf(&tmp, opad, crypto_tfm_alg_blocksize(tfm));

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
	
	sg_set_buf(&tmp, out, crypto_tfm_alg_digestsize(tfm));
	
	crypto_digest_update(tfm, &tmp, 1);
	crypto_digest_final(tfm, out);
}

void crypto_hmac(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen,
                 struct scatterlist *sg, unsigned int nsg, u8 *out)
{
#ifdef CONFIG_SL2312_IPSEC
	int i, error = 0;
	struct scatterlist tmpsc;
	unsigned int plen=0;
	wait_queue_t wait;
			
	crypto_digest_init(tfm);
	 
	if(*keylen > tfm->__crt_alg->cra_blocksize)
	{
		unsigned char *tk = kmalloc(tfm->__crt_alg->cra_blocksize, GFP_KERNEL);
		if (!tk) {
			printk(KERN_ERR "%s: tk is not allocated\n", __FUNCTION__);
			error = -ENOMEM;

		}
		
		memset(tk, 0, tfm->__crt_alg->cra_blocksize);

		tmpsc.page = virt_to_page(key);
		tmpsc.offset = offset_in_page(key);
		tmpsc.length = *keylen;

		tfm->crt_digest.dit_init(tfm);
		tfm->crt_digest.dit_update(tfm, &tmpsc, nsg);
		tfm->crt_digest.dit_final(tfm, tk);
		
		memcpy(key,tk,tfm->__crt_alg->cra_blocksize);
		*keylen = tfm->__crt_alg->cra_blocksize;
   	
		kfree(tk);		
	}

	for(i=0;i<nsg;i++)
	{
		plen += sg[i].length;	
	}
			
	init_waitqueue_entry(&wait, current);
	memset(&auth_hmac_op,0x00,sizeof(struct IPSEC_PACKET_S));   
	auth_hmac_op.op_mode = AUTH;
	auth_hmac_op.auth_algorithm = ipsec_get_auth_algorithm((unsigned char *)tfm->__crt_alg->cra_name,1); //(0) AUTH; (1) HMAC
	auth_hmac_op.auth_key_size = *keylen;
	memcpy(auth_hmac_op.auth_key, key, *keylen);
	
	auth_hmac_op.out_packet = kmalloc(IPSEC_TEXT_LEN,GFP_DMA);//GFP_KERNEL);

	auth_hmac_op.callback = auth_hmac_callback;
	auth_hmac_op.auth_header_len = 0;
	auth_hmac_op.in_packet = kmap(sg->page) + sg->offset;
	if(nsg>1)
		sg->length = plen;
	auth_hmac_op.pkt_len = sg->length;
	auth_hmac_op.auth_algorithm_len = sg->length;
	auth_hmac_done = 0; 
	//consistent_sync(auth_hmac_op.out_packet, IPSEC_TEXT_LEN, DMA_BIDIRECTIONAL);//PCI_DMA_FROMDEVICE); 
	ipsec_crypto_hw_process(&auth_hmac_op);	
	
	add_wait_queue(&auth_hmac_queue, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (auth_hmac_done) /* whatever test your driver needs */
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&auth_hmac_queue, &wait);
	consistent_sync(auth_hmac_op.out_packet, IPSEC_TEXT_LEN, DMA_BIDIRECTIONAL);//PCI_DMA_FROMDEVICE); 
	memcpy(out, (u8 *)(auth_hmac_op.out_packet+auth_hmac_op.pkt_len),crypto_tfm_alg_digestsize(tfm));
	
	kfree(auth_hmac_op.out_packet); 

	
#else	
	crypto_hmac_init(tfm, key, keylen);
	crypto_hmac_update(tfm, sg, nsg);
	crypto_hmac_final(tfm, key, keylen, out);
#endif
}

EXPORT_SYMBOL_GPL(crypto_hmac_init);
EXPORT_SYMBOL_GPL(crypto_hmac_update);
EXPORT_SYMBOL_GPL(crypto_hmac_final);
EXPORT_SYMBOL_GPL(crypto_hmac);

