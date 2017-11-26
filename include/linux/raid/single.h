#ifndef _SINGLE_H
#define _SINGLE_H

#include <linux/raid/md.h>

struct sibio_s {
	mddev_t	*mddev;
	struct bio *master_bio;
};

typedef struct sibio_s sibio_t;

struct si_private_data_s {
	mddev_t	*mddev;
	mdk_rdev_t *rdev;
	mempool_t *sibio_pool;
};

typedef struct si_private_data_s si_conf_t;

/*
 * this is the only point in the RAID code where we violate
 * C type safety. mddev->private is an 'opaque' pointer.
 */
#define mddev_to_conf(mddev) ((si_conf_t *) mddev->private)

#endif
