/*
 * single.c : Single Device driver for Accusys
 *
 * Copyright (C) 2005 Luke Xia, Accusys Nanjing.
 */

#include <linux/raid/single.h>

#include <linux/acs_nas.h>
#include <linux/acs_char.h>

#define DEBUG 0
#define dprintk(x...) ((void)(DEBUG && printk(x)))

/* Number of guaranteed sibios in case of extreme VM load: */
#define NR_SI_BIOS 256

static mdk_personality_t si_personality;

static void unplug_slaves(mddev_t *mddev);


static void *sibio_pool_alloc(unsigned int __nocast gfp_flags, void *data)
{
	sibio_t *sibio = NULL;
	int size = sizeof(sibio_t);

	sibio = kmalloc(size, gfp_flags);
	if (sibio)
		memset(sibio, 0, size);
	return sibio;
}

static void sibio_pool_free(void *sibio, void *data)
{
	kfree(sibio);
}

static int si_end_request(struct bio *bio, unsigned int bytes_done, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	sibio_t *sibio = (sibio_t *)(bio->bi_private);
	struct bio *master_bio = sibio->master_bio;
	si_conf_t *conf = mddev_to_conf(sibio->mddev);

	if (bio->bi_size)
		return 1;

	if (!uptodate)
		md_error(sibio->mddev, conf->rdev);

	bio_endio(master_bio, master_bio->bi_size, uptodate? 0: -EIO);
	mempool_free(sibio, conf->sibio_pool);
	bio_put(bio);
	rdev_dec_pending(conf->rdev, conf->mddev);
	return 0;
}

static void unplug_slaves(mddev_t *mddev)
{
	si_conf_t *conf = mddev_to_conf(mddev);
	mdk_rdev_t *rdev = conf->rdev;

	rcu_read_lock();
	if (rdev && !rdev->faulty && atomic_read(&rdev->nr_pending)) {
		request_queue_t *r_queue = bdev_get_queue(rdev->bdev);

		atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();

		if (r_queue->unplug_fn)
			r_queue->unplug_fn(r_queue);

		rdev_dec_pending(rdev, mddev);
		rcu_read_lock();
	}
	rcu_read_unlock();
}

static void si_unplug(request_queue_t *q)
{
	unplug_slaves(q->queuedata);
}

static int si_issue_flush(request_queue_t *q, struct gendisk *disk,
			     sector_t *error_sector)
{
	mddev_t *mddev = q->queuedata;
	si_conf_t *conf = mddev_to_conf(mddev);
	mdk_rdev_t *rdev = conf->rdev;
	int ret = 0;

	rcu_read_lock();
	if (rdev && !rdev->faulty) {
		struct block_device *bdev = rdev->bdev;
		request_queue_t *r_queue = bdev_get_queue(bdev);

		if (!r_queue->issue_flush_fn)
			ret = -EOPNOTSUPP;
		else {
			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			ret = r_queue->issue_flush_fn(r_queue, bdev->bd_disk,
						      error_sector);
			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
	}
	rcu_read_unlock();
	return ret;
}

static int make_request(request_queue_t *q, struct bio *bio)
{
	mddev_t *mddev = q->queuedata;
	si_conf_t *conf = mddev_to_conf(mddev);
	mdk_rdev_t *rdev = conf->rdev;
	struct bio *si_bio = NULL;
	sibio_t *sibio = NULL;
//allen
#if 0
	if (bio_data_dir(bio)==WRITE) {
		disk_stat_inc(mddev->gendisk, writes);
		disk_stat_add(mddev->gendisk, write_sectors, bio_sectors(bio));
	} else {
		disk_stat_inc(mddev->gendisk, reads);
		disk_stat_add(mddev->gendisk, read_sectors, bio_sectors(bio));
	}
#endif
	if (bio_data_dir(bio)==WRITE) {
                disk_stat_inc(mddev->gendisk, ios[0]);
                disk_stat_add(mddev->gendisk, sectors[0], bio_sectors(bio));
        } else {
                disk_stat_inc(mddev->gendisk, ios[1]);
                disk_stat_add(mddev->gendisk, sectors[1], bio_sectors(bio));
        }
	
	sibio = mempool_alloc(conf->sibio_pool, GFP_NOIO);

	sibio->master_bio = bio;
	sibio->mddev = mddev;

	/* fail if the disk is !operational */
	if ((mddev->state & (1 << MD_SB_ERRORS)) || 
	    !rdev || rdev->faulty || !rdev->in_sync) 
		goto outerr;

	rcu_read_lock();
	atomic_inc(&rdev->nr_pending);
	rcu_read_unlock();

	si_bio = bio_clone(bio, GFP_NOIO);

	/* check if !rdev when bio_clone (may sleep) */
	if (!rdev || rdev->faulty || !rdev->in_sync) {
		if (rdev)
			rdev_dec_pending(rdev, mddev);
		goto outerr;
	}

	si_bio->bi_private = sibio;
	si_bio->bi_sector = bio->bi_sector + rdev->data_offset;
	si_bio->bi_bdev = rdev->bdev;
	si_bio->bi_end_io = si_end_request;

	if (bio_data_dir(bio)==WRITE) 
		si_bio->bi_rw = WRITE;
	else 
		si_bio->bi_rw = READ;

	generic_make_request(si_bio);
	return 0;
outerr:
	if (si_bio)
		bio_put(si_bio);
	if (sibio)
		mempool_free(sibio, conf->sibio_pool);
	bio_io_error(bio, bio->bi_size);
	return 0;
}

static void status(struct seq_file *seq, mddev_t *mddev)
{
	si_conf_t *conf = mddev_to_conf(mddev);

	seq_printf(seq, " [1/1] [");
	seq_printf(seq, "%s",
	      (conf->rdev && conf->rdev->in_sync) ? "U" : "_");
	seq_printf(seq, "]");
}


static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	si_conf_t *conf = mddev_to_conf(mddev);

	if (rdev != conf->rdev)
		return;

	if (rdev->in_sync) 
		mddev->degraded++;

	if (mddev->degraded) {
		mddev->state |= (1 << MD_SB_ERRORS);
		write_kernellog("%d Volume%d: disk of single disk was failed.", ERROR_LOG, md_to_volume(mddev));
		lcd_string(LCD_L2, "V%d fails!", md_to_volume(mddev));
		buzzer_on(BUZZ_VOL(md_to_volume(mddev)), ACS_BUZZ_ERR);
	}
	
	rdev->in_sync = 0;
	rdev->faulty = 1;
	mddev->sb_dirty = 1;
}

static int si_remove_disk(mddev_t *mddev, int number)
{
	si_conf_t *conf = mddev->private;
	int err = 0;
	mdk_rdev_t *rdev = conf->rdev;

	if (rdev) {
		if (rdev->in_sync ||
		    atomic_read(&rdev->nr_pending)) {
			err = -EBUSY;
			goto abort;
		}
		conf->rdev = NULL;
		synchronize_rcu();
		if (atomic_read(&rdev->nr_pending)) {
			/* lost the race, try later */
			err = -EBUSY;
			conf->rdev = rdev;
		}
	}
abort:
	return err;
}

static int run(mddev_t *mddev)
{
	si_conf_t *conf;
	mdk_rdev_t *rdev;
	struct list_head *tmp;

	if (mddev->level != LEVEL_SINGLE) {
		printk("single: %s: raid level not set to single disk (%d)\n",
		       mdname(mddev), mddev->level);
		goto out;
	}

	conf = kmalloc(sizeof(si_conf_t), GFP_KERNEL);
	mddev->private = conf;
	if (!conf)
		goto out_no_mem;

	memset(conf, 0, sizeof(*conf));
	conf->sibio_pool = mempool_create(NR_SI_BIOS, sibio_pool_alloc,
			    sibio_pool_free, NULL);
	if (!conf->sibio_pool)
		goto out_no_mem;

	ITERATE_RDEV(mddev, rdev, tmp) {
		/* 
		 * actually, one and only one 'rdev' is in list of mddev->disks 
		 * :) get 'rdev' directly is ok
		 */
		if (conf->rdev)
			BUG(); /* BUG if more than one disk in list */
		conf->rdev = rdev;

		blk_queue_stack_limits(mddev->queue,
				       rdev->bdev->bd_disk->queue);
		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, so limit ->max_sector to one PAGE, as
		 * a one page request is never in violation.
		 */
		if (rdev->bdev->bd_disk->queue->merge_bvec_fn &&
		    mddev->queue->max_sectors > (PAGE_SIZE>>9))
			blk_queue_max_sectors(mddev->queue, PAGE_SIZE>>9);

	}
	conf->mddev = mddev;

	if (!conf->rdev) {
		printk(KERN_ERR "single: no operational disk for %s\n",
			mdname(mddev));
		mddev->state |= (1 << MD_SB_ERRORS);
		if (mdidx(mddev) != 0) {
			write_kernellog("%d Volume%d: the disk of single disk was failed.", ERROR_LOG, md_to_volume(mddev));
			lcd_string(LCD_L2, "V%d fails!", md_to_volume(mddev));
			buzzer_on(BUZZ_VOL(md_to_volume(mddev)), ACS_BUZZ_ERR);
		}
		goto out_free_conf;
	}

	/*
	 * Ok, everything is just fine now
	 */
	mddev->array_size = mddev->size;

	mddev->queue->unplug_fn = si_unplug;
	mddev->queue->issue_flush_fn = si_issue_flush;

	return 0;

out_no_mem:
	printk(KERN_ERR "single: couldn't allocate memory for %s\n",
	       mdname(mddev));

out_free_conf:
	if (conf) {
		if (conf->sibio_pool)
			mempool_destroy(conf->sibio_pool);
		kfree(conf);
		mddev->private = NULL;
	}
out:
	return -EIO;
}

static int stop(mddev_t *mddev)
{
	si_conf_t *conf = mddev_to_conf(mddev);

	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	if (conf->sibio_pool)
		mempool_destroy(conf->sibio_pool);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

#ifdef MD_ADD_IN_ERROR
/*
 * si_add_disk_in_error::add the disk as a full functional disk to the ERROR single disk.
 * params: mddev, rdev
 * return (0)-success, (-EBUSY)-fail
 */
static int si_add_disk_in_error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	si_conf_t *conf = mddev->private;
	
	if (conf->rdev)
		return -EBUSY;
	conf->rdev = rdev;

	mddev->update_fail = 0; /* clear update_fail */
	mddev->state &= ~(1<<MD_SB_ERRORS);
	buzzer_off(BUZZ_VOL(md_to_volume(mddev)), ACS_BUZZ_ERR);
	return 0;
}
#endif /* MD_ADD_IN_ERROR */

static mdk_personality_t si_personality =
{
	.name		= "single",
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_remove_disk= si_remove_disk,

#ifdef	MD_ADD_IN_ERROR
	/* removed disk back */
	.hot_add_disk_in_error 	= si_add_disk_in_error,
#endif 	/* MD_ADD_IN_ERROR */
};

static int __init single_init(void)
{
	return register_md_personality(SINGLE, &si_personality);
}

static void single_exit(void)
{
	unregister_md_personality(SINGLE);
}

module_init(single_init);
module_exit(single_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("md-personality-11"); /* SINGLE */
