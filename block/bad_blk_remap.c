#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>      /* for max_pfn/max_low_pfn */
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/ide.h>
#include <linux/buffer_head.h>
#include <asm/types.h>
#include <linux/acs_nas.h>
#include <linux/acs_char.h>
#include <linux/hotswap.h>
#include <linux/bad_blk_remap.h>


#ifdef	BAD_BLK_REMAP
mempool_t *vbiopool;
kmem_cache_t *vbiozone;
remap_info_t remap_infos[DISK_NUM];
extern int __make_request(request_queue_t *q, struct bio *bio);
extern uint32_t __div64_32(uint64_t *n, uint32_t base);
//DEFINE_SPINLOCK(remap_lock);
wait_queue_head_t remap_queue;
struct semaphore work_remap_lock;
int work_daemon(void);
int remap_write_count = 0;
int remap_read_count = 0;
extern uint32_t __div64_32(uint64_t *n, uint32_t base);

extern int make_bio(struct bio*, struct bio**, uint64_t, uint64_t, remap_entry_t *, vbio_endio_t *);

void remap_set_hwgroup(unsigned char disknum)
{
	unsigned char prm_scd, mst_slv;
        ide_drive_t *drive;

	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &ide_hwifs[prm_scd].drives[mst_slv];
        HWGROUP(drive)->rq = NULL;
}

DEFINE_SPINLOCK(remap_lock);
struct list_head work_list;

static void vbio_init_once(void *foo, kmem_cache_t *cachep, unsigned long flag)
{
	vbio_endio_t *vbio = (vbio_endio_t *)foo;
	memset(vbio, 0, sizeof(vbio_endio_t));
}

void init_vbio(void)
{
	vbiozone = kmem_cache_create("vbio_zone", sizeof(vbio_endio_t), 0,
			SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT, 
				vbio_init_once, NULL);
	if (!vbiozone) {
		printk(" create vbio zone error!\n");
		return;
	}
	vbiopool = mempool_create(32, mempool_alloc_slab, mempool_free_slab,
						vbiozone);
	if(!vbiopool){
		printk("create vbio pool error!\n");
	}
}

void exit_vbio(void)
{
	mempool_destroy(vbiopool);
	kmem_cache_destroy(vbiozone);
}

vbio_endio_t *endio_alloc(void)
{
	vbio_endio_t *vbio;
	vbio = mempool_alloc(vbiopool, GFP_NOIO);
	memset(vbio, 0, sizeof(vbio_endio_t));
	return vbio;
}

void endio_free(vbio_endio_t *vbio)
{
	mempool_free(vbio, vbiopool);
}


int my_bio_endio(struct bio *bio, unsigned int arg, int error)
{
	vbio_endio_t *endio = bio->bi_private;
	struct bio *p_bio;

	if (error) {
		endio->error = error;
	}
	if(!bio->bi_size) {
		endio->io_remaining--;
	}
	if (endio->io_remaining) { 
		if(!bio->bi_size){ 
			bio_put(bio);
		}
	} else {
		int size;
		p_bio = endio->p_bio;
		bio_put(bio);
		size = p_bio->bi_size;
		p_bio->bi_sector += (p_bio->bi_size >> 9);
		p_bio->bi_size = 0;
		if (endio->error) {
			clear_bit(BIO_UPTODATE, &p_bio->bi_flags);
		}
#ifdef ACS_DEBUG
		printk(KERN_EMERG " call parent bio endio fn!\n");
#endif
		p_bio->bi_end_io(p_bio, size, endio->error);
		endio_free(endio);
	}
	return 0;
}

static void snapshot_bio_map(struct bio *bio, int done_sector, struct bio *pbio,
						int bsize)
{
	int i;	
	unsigned long size, done_size;

	size = 0;
	done_size = done_sector * 512;

	/* find the right index of bi_io_vec */
	for (i = 0; i < bio->bi_vcnt; i++) {
		size += bio->bi_io_vec[i].bv_len;
		if (size >= done_size)
			break;
	}

	if (size > done_size) {
		if (size < done_size + bsize) {
			/* pbio's page size cover two or more bv_page in bio */ 
			if (bio_add_page(pbio, bio->bi_io_vec[i].bv_page, size - done_size,
					bio->bi_io_vec[i].bv_offset + 
					bio->bi_io_vec[i].bv_len - (size - done_size))
					< size - done_size) BUG();
		} else {
			/* only need add one bv_page to pbio */
			if (bio_add_page(pbio, bio->bi_io_vec[i].bv_page, bsize,
					bio->bi_io_vec[i].bv_offset + 
					bio->bi_io_vec[i].bv_len - (size - done_size))
					< bsize) BUG();
			return;
		}
	}

	/* 
	* go to next bv_page if
	* size = done_size or size < done_size + bsize.
	*/
	i++;
	for (; i < bio->bi_vcnt; i++) {
		size += bio->bi_io_vec[i].bv_len;
		if (size < (done_size + bsize))
			if (bio_add_page(pbio, bio->bi_io_vec[i].bv_page,
				bio->bi_io_vec[i].bv_len,
				bio->bi_io_vec[i].bv_offset)
				< bio->bi_io_vec[i].bv_len) BUG();
		if (size == (done_size + bsize)) {
			if (bio_add_page(pbio, bio->bi_io_vec[i].bv_page,
					bio->bi_io_vec[i].bv_len,
					bio->bi_io_vec[i].bv_offset)
					< bio->bi_io_vec[i].bv_len);
				break;
		}
		if (size > (done_size + bsize)) {
			if (bio_add_page(pbio, bio->bi_io_vec[i].bv_page,
					bio->bi_io_vec[i].bv_len - (size - (done_size + bsize)),
					bio->bi_io_vec[i].bv_offset)
					< bio->bi_io_vec[i].bv_len - (size - (done_size + bsize)));
				break;
		}
	}
	return;
}

void 
intersection(extent_t *ext, remap_entry_t *entry, extent_t *ret)
{
	uint64_t end;
	ret->start = max(ext->start, entry->old_sec);
	end = min(ext->end, entry->old_sec +  entry->len - 1);
	ret->end = end;
}


int 
bio_remap_check(struct bio *bio, extent_t *ext, block_remap_t *blk_remap, 
	struct bio **head, vbio_endio_t *endio) 
{
	block_remap_t *remap;
	remap_entry_t *entry;
	int map = 0;
	extent_t inter;
	int count = 0;

	remap = blk_remap->next;
	while(remap != blk_remap) {
		entry = &remap->remap;
		if(ext->end < entry->old_sec) {
#ifdef ACS_DEBUG
			printk("end of bio is less than entry\n");
#endif
			goto no_remap;
		} else if(ext->start > entry->old_sec + entry->len - 1) {
			remap = remap->next;
#ifdef ACS_DEBUG
			printk(KERN_EMERG " start of bio is more than end of entry\n");
#endif
			continue;	
		} else {
			map = 1;
#ifdef ACS_DEBUG
			printk(KERN_EMERG " bio and entry has intersection\n");
#endif
			intersection(ext, entry, &inter);
			if(inter.start > ext->start) {
#ifdef ACS_DEBUG
			printk(KERN_EMERG " start of intersection more than start of bio\n");
#endif
				make_bio(bio, head, ext->start, inter.start - 1, NULL, endio);
				make_bio(bio,head,inter.start, inter.end,entry, endio);
				count += 2;
			} else {
#ifdef ACS_DEBUG
			printk(KERN_EMERG "** start of intersection not more than start of bio\n");
#endif
				make_bio(bio,head,inter.start, inter.end,entry, endio);
				count++;
			}
			if(inter.end < ext->end) {
#ifdef ACS_DEBUG
			printk(KERN_EMERG " end of intersection less than end of bio\n");
#endif
				ext->start = inter.end + 1;
				remap = remap->next;
				continue;
			} else if(inter.end == ext->end) {
#ifdef ACS_DEBUG
			printk(KERN_EMERG " end of intersection eqeue to end of bio\n");
#endif
				break;
			}
		}

		remap = remap->next;
	}
	return count;

no_remap:
#ifdef ACS_DEBUG
	printk(KERN_EMERG " end of intersection not eqeue to end of bio\n");
#endif
	make_bio(bio,head,ext->start, ext->end, 0, endio);
	count++;
	return count;
}

/** 
 * make_bio - 	split the bio which covers several segments.
 * @bio:		the current bio 
 * @pbios:	the bios after split, each  of which covers only one segment
 * @n:		the segments which the current bio covers
 * @seglog:	the segment bits
 * @sector:	the sector number of the current bio
*/
int make_bio(struct bio *bio, struct bio **head,uint64_t start, uint64_t end,
				remap_entry_t *entry, vbio_endio_t *endio)
{
	struct bio *pbio,*tmpbio,*next;
	uint64_t sector;
	unsigned long len;
	
	sector = bio->bi_sector;
	len = end - start + 1;
	
	pbio = bio_alloc(GFP_NOIO, bio->bi_vcnt);
	if(entry) {
		pbio->bi_sector = entry->new_sec + (start - entry->old_sec);
	} else {
		pbio->bi_sector = start;
	}
	pbio->bi_bdev = bio->bi_bdev;
	pbio->bi_rw = bio->bi_rw;
	pbio->bi_private = endio;
	pbio->bi_end_io = my_bio_endio;
	pbio->bi_next = NULL;
	snapshot_bio_map(bio, start-sector, pbio, len * 512);
	if(!(*head)) *head = pbio;
	else {  
		tmpbio = *head;
		next = tmpbio->bi_next;
		while(next) {
			tmpbio = next;
			next = tmpbio->bi_next;
		}
		tmpbio->bi_next = pbio;
	}
	return 0;
} 
int 
badblk_bio_remap(struct bio *bio, struct bio **head)
{
	ide_drive_t *drive = bio->bi_bdev->bd_disk->queue->queuedata;
	int index = drive->hwif->index;
	int ret =0;
	struct vbio_endio *endio;
	remap_info_t *remap_info = &remap_infos[index];
	remap_descriptor_t *remap_descriptor = &(remap_info->remap_descriptor);
	block_remap_t *block_remap;
	extent_t ext;
	uint64_t mod_no;
	
	if(!remap_descriptor->count) {
		return 0;
	}
	endio = endio_alloc();
	endio->p_bio = bio;
	mod_no = 0;
	ext.start = bio->bi_sector;
	ext.end = (bio->bi_size >> 9) + bio->bi_sector - 1;
	block_remap = &remap_info->block_remap[mod_no];
	ret = bio_remap_check(bio,&ext, block_remap, head, endio);

	return ret;
}

int accu_make_request(request_queue_t *q, struct bio *bio)
{
	struct bio *head,*next,*tmp;
	int ret;
	struct ide_drive_s *drive;
	vbio_endio_t *endio;
	head = NULL;
#ifdef 	BAD_BLK_REMAP
	drive = (ide_drive_t *)q->queuedata;
	if(drive->present){

		ret = badblk_bio_remap(bio, &head);
		if(!ret)
			return __make_request(q, bio);
		next = head;
		while(next) {
			endio = (vbio_endio_t*)next->bi_private;
			endio->io_remaining = ret;
			next = next->bi_next;
		}
		next = head;
		while(next) {
			tmp = next->bi_next;
			next->bi_next = NULL;
			if((ret = __make_request(q,next))) {
				BUG();
			}
			next = tmp;
		}
		return ret;
	} else 
		return __make_request(q,bio);
#endif
	return __make_request(q, bio);
	
}


/******************************************************************************
 * Function:    set_remap_table_invalid()
 * Description: set remap table invalid when we pull out the disk.
 * Input:       index
 * return:      None
 *****************************************************************************/
void set_remap_table_invalid(int index){
        remap_info_t *remap_info = &remap_infos[index];
#ifdef  ACS_DEBUG
        printk("set_remap_table_invalid(): index = %d\n", index);
#endif  /* _ACS_DEBUG */
        remap_info->stat = REMAP_TABLE_INVALID;
}

/**
 * Function:    init_block_remap()
 * Description: initialize remap_tables[] structure when initializing disk
 * Input:      	disknum 
 * return:      0 on success, otherwise -1
 */
int init_block_remap(unsigned char disknum)
{
        int i;
        sector_t capacity;
	unsigned char prm_scd, mst_slv;
        ide_drive_t *drive;
        struct gendisk *disk;
        remap_info_t *remap_info = &(remap_infos[disknum]);
        remap_descriptor_t *remap_descriptor = &(remap_info->remap_descriptor);

#ifdef	ACS_DEBUG
	acs_printk("%s: start\n", __func__);
#endif
	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &ide_hwifs[prm_scd].drives[mst_slv];
	disk = ((struct ide_disk_obj *)drive->driver_data)->disk;

        capacity = drive->capacity64;
        memset(remap_info, 0, sizeof(remap_info_t));
        remap_info->stat = REMAP_TABLE_INVALID;

        remap_descriptor->start_sec = REMAPPED_START_ADDRESS + disk->part[2]->start_sect + drive->sect0;
#ifdef	ACS_DEBUG
	acs_printk("%s: star_sec=%llu\n", __func__, remap_descriptor->start_sec);
#endif
        remap_descriptor->end_sec = REMAPPED_START_ADDRESS + disk->part[2]->start_sect + drive->sect0;
        remap_descriptor->flag = 1;

        for (i = 0; i < 64 ; i++){
                block_remap_t  *block_remap = &(remap_info->block_remap[i]);
                block_remap->next = block_remap;
                block_remap->no = 0;
        }
        return 0;
}

/*** Sunny 2005.12.17 begin ***/
int init_block_remap_test(int index, remap_descriptor_test_t *remap_descriptor_test)
{
        sector_t capacity;
        ide_hwif_t *hwif = &ide_hwifs[index];
        ide_drive_t *drive = &(hwif->drives[0]);
        struct ide_disk_obj *idkp;
        struct gendisk *disk;
        remap_info_t *remap_info = &remap_infos[index];
        block_remap_test_t  *block_remap_test = &(remap_info->block_remap_test);

        idkp = (struct ide_disk_obj *)(drive->driver_data);
        disk = idkp->disk;
        capacity = hwif->drives[0].capacity64;
        remap_info->stat = REMAP_TABLE_INVALID;
        memset(remap_info, 0, sizeof(remap_info_t));

        remap_descriptor_test->start_sec = REMAPPED_START_ADDRESS;
        remap_descriptor_test->end_sec = REMAPPED_START_ADDRESS;
        remap_descriptor_test->count = 0;
        block_remap_test->next = block_remap_test;
        block_remap_test->no = 0;

        return 0;
}

/*** Sunny 2005.12.17 end ***/

int work_daemon(void)
{
        struct list_head *p, *n;
        work_remap_t *work;
        int pid;

        while(1){
                spin_lock_irq(&remap_lock);
                list_for_each_safe(p, n, &work_list){
                        list_del(p);
                        work = list_entry(p, work_remap_t, list);
                        pid = kernel_thread(work->p_fn, work->bio, CLONE_FS|CLONE_SIGHAND);                        if(pid < 0){
                                printk("work_daemon():start thread fail!\n");
                                return -1;
                        }
               //      if(pid < 0)
                //      work->p_fn(work->bio);
                        kfree(work);
                }
                spin_unlock_irq(&remap_lock);
                sleep_on_timeout(&remap_queue, 1);
        }
}
/*******************************************************************************
 * Function:    delete_remap_signature()
 * Description: delete disk remap signature while testing
 * Input:       index
 * return       0 on success, otherwise -1;
 ******************************************************************************/
void delete_remap_signature(int index){
        remap_info_t *remap_info = &remap_infos[index];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;

        if (remap_info->stat == REMAP_TABLE_INVALID)
                return ;
        remap_descriptor->count = 0;
        rw_remap_sector(index, (char *)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        return;
}

/*******************************************************************************
 * Function:    create_remap_signature()
 * Description: create disk remap signature while disk initailized or hotswap
 *              include empty remap structure
 * Input:       index
 * return:      0 on success, otherwise -1;
 ******************************************************************************/
int create_remap_signature(int index){
        unsigned char buf[512];
        int i;

        for (i = 0 ; i < 512; i++)
                buf[i] = (unsigned char) i;

        if(rw_remap_sector(index,(char*)buf,WIN_WRITE,REMAP_SIGNATURE_ADDRESS)){
#ifdef ACS_DEBUG
                printk("create_signature():read / write disk fail !\n");
#endif
                return (-1);
        }
        return 0;
}

/*******************************************************************************
 * Function:    revise_partition_value()
 * Description: if the signature is old, then read all remap tables, revise the
 *              part and write them to the disk
 * Input:
 * return:      0 on success, otherwise -1;
 ******************************************************************************/
int revise_partition_value(int index, int count)
{
        remap_entry_t remap_table[32];
        int i = 0;
        count = (count >> 16) + (count & 0xffff);
#ifdef  ACS_DEBUG
        printk("revise_partition_value(): index = %d, count = %d\n", index, count);
#endif  /* ACS_DEBUG */
        while (count > 0){
                rw_remap_sector(index, (char *)&remap_table, WIN_WRITE, REMAP_TABLE_ADDRESS+i);
                count = count - 32;
                i++;
        }
#ifdef _ACS_DEBUG
        printk(" i is %d\n", i);
#endif
        return 0;
}

/*******************************************************************************
 * Function:    check_remap_signature()
 * Description: on the boot stage, check disk remap table signature if valid
 * Input:       major, minor
 * return:      0 if valid, otherwise -1
 ******************************************************************************/
int check_remap_signature(int index){
        int i;
        unsigned long long check_sum;
        unsigned char      buf[512];
        remap_info_t *remap_info = &remap_infos[index];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;

        /*** Sunny 2005.12.14 begin ***/
        if(rw_remap_sector(index,buf,WIN_READ,REMAP_SIGNATURE_ADDRESS)){
#ifdef ACS_DEBUG
                printk("check_sig():read remap signature address fail!\n");
#endif
                return (-1);
        }
        /*** Sunny 2005.12.14 end ***/

        for (i = 0; i < 512; i++) {
                if ((buf[i]&0xFF) != (unsigned char) i){
                        printk(" buf[%d] = 0x%x", i, buf[i]);
                        return 1;
                }
        }

        /*** Sunny 2005.12.14 begin ***/
        if(rw_remap_sector(index, buf, WIN_READ, REMAP_DESCRIPTOR_ADDRESS)){
#ifdef ACS_DEBUG
                printk("check_sig():read remap signature address fail!\n");
#endif
                return (-1);
        }
        /*** Sunny 2005.12.14 end ***/
         /*** Sunny 2005.12.13  ***/
        memcpy(remap_descriptor, buf, sizeof(remap_descriptor_t));

        if (buf[0] == 1){
                        printk("check_remap_signature: new remap_descriptor\n");
                        //memcpy(remap_descriptor, buf, sizeof(remap_descriptor_t));
                        check_sum = remap_descriptor->start_sec + remap_descriptor->end_sec                                        + remap_descriptor->count;
                        if(remap_descriptor->check_sum != check_sum){
                                return 1;
                        }
        }

        return 0;
        /*** Sunny 2005.12.13 end ***/
}

/**
 * Function:    insert_remap_table()
 * Description: insert badblk_remap into hash table
 * Input:       disknum, block_remap
 * return:      0 on success, otherwise -1;
 */
int insert_remap_table(unsigned char disknum, block_remap_t *block_remap)
{
        uint64_t mod_no;
        block_remap_t *head, *next, *tmp;
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
//      sector_t old_sec = block_remap->remap.old_sec;
//      mod_no = block_remap->remap.old_sec;
//      __div64_32(&mod_no,remap_descriptor->part_size);
//      if(mod_no >= 64) mod_no = 63;
        mod_no = 0;
        block_remap->no = remap_descriptor->count;
        head = (block_remap_t *)&remap_info->block_remap[mod_no];
        next = head->next;
        tmp = head;

        while(next != head && (next->remap.old_sec < block_remap->remap.old_sec)){
                tmp = next;
                next = next->next;
        }

        tmp->next = block_remap;
        block_remap->next = next;
        head->no++;
        return 0;
}

/**
 * Function:    my_read_remap_table()
 * Description: read badblk_remap and insert into hash table
 * Input:       count
 * return:	0 on success, otherwise -1
 */
int 
my_read_remap_table(int disknum, int count,struct gendisk *disk,struct ide_drive_s *drive)
{
        block_remap_t *block_remap;
        remap_entry_t remap_table[32];
        remap_table_t remap_table1[32];
        remap_entry_t *remap_entry_temp = kmalloc(sizeof(remap_entry_t) * 512, GFP_ATOMIC);
        int ret = 0, tmp_count = (count >> 16) + (count & 0xffff);
        int i = 0;
        int m = 0;
        int minor=0;

        /*** Sunny 2005.12.13 begin ***/
        ret = rw_remap_sector(disknum, (char*)&remap_table, WIN_READ, REMAP_TABLE_ADDRESS+i);
        if ((remap_table[0].len & 0xffffffff) == 0){
                while (tmp_count > 0){
                        int j = 0;
                        ret = rw_remap_sector(disknum, (char*)&remap_table1, WIN_READ, REMAP_TABLE_ADDRESS+i);

                        if (tmp_count > 32){
                                for (j = 0; j < 32; j ++){
                                        block_remap = kmalloc(sizeof(block_remap_t), GFP_KERNEL);
                                        memset(block_remap, 0, sizeof(block_remap_t));
                                        minor = remap_table1[j].part;
                                      remap_table[j].old_sec = remap_table1[j].old_sec + disk->part[minor - 1]->start_sect + drive->sect0;
                                        remap_table[j].new_sec = remap_table1[j].new_sec + disk->part[2]->start_sect + drive->sect0;
                                        remap_table[j].len = 8;

                                        remap_entry_temp[m] = remap_table[j];
                                        m++;
                                        memcpy(&block_remap->remap, &remap_table[j], sizeof(remap_entry_t));
                                        insert_remap_table(disknum, block_remap);
                                }
                        }else{
                                for (j = 0 ; j < tmp_count; j++ ){
                                        block_remap = kmalloc(sizeof(block_remap_t), GFP_KERNEL);
                                        memset(block_remap, 0, sizeof(block_remap_t));
                                        minor = remap_table1[j].part;
                                        remap_table[j].old_sec = remap_table1[j].old_sec + disk->part[minor - 1]->start_sect + drive->sect0;
                                        remap_table[j].new_sec = remap_table1[j].new_sec + disk->part[2]->start_sect + drive->sect0;
                                        remap_table[j].len = 8;
                                        remap_entry_temp[m] = remap_table[j];
                                        m++;
                                        memcpy(&block_remap->remap, &remap_table[j], sizeof(remap_entry_t));
                                        insert_remap_table(disknum, block_remap);
                                }
                        }
                        tmp_count = tmp_count -32;
                        i++;
                }

                for (i = 0; i < 16; i++){
                        if (rw_remap_sector(disknum, (char *)&remap_entry_temp[32*i], WIN_WRITE, (REMAP_TABLE_ADDRESS+i))){
                        return -1;
                        }
                }

        }else{
                while (tmp_count > 0){
                        int j = 0;

                        if (tmp_count > 32){
                                for (j = 0; j < 32; j ++){
                                        block_remap = kmalloc(sizeof(block_remap_t), GFP_KERNEL);
                                        memset(block_remap, 0, sizeof(block_remap_t));
                                        memcpy(&block_remap->remap, &remap_table[j], sizeof(remap_entry_t));
                                        insert_remap_table(disknum, block_remap);
                                }
                        }else{
                                for (j = 0 ; j < tmp_count; j++ ){
                                        block_remap = kmalloc(sizeof(block_remap_t), GFP_KERNEL);
                                        memset(block_remap, 0, sizeof(block_remap_t));
                                        memcpy(&block_remap->remap, &remap_table[j], sizeof(remap_entry_t));
                                        insert_remap_table(disknum, block_remap);
                                }
                        }
                        tmp_count = tmp_count -32;
                        i++;
                }
        }

        return 0;
}

int create_remap_table(unsigned char);
/**
 * Function:    load_remap_table()
 * Description: on the boot stage or hotswap, we should load remap table from 
 * disk to memory
 * Input:      	disknum 
 * return:      0 on success, otherwise -1
 */
int load_remap_table(int disknum)
{
        char buf[512];
        int ret;
        unsigned char prm_scd, mst_slv;
        remap_info_t *remap_info =  &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
        remap_descriptor_test_t *remap_descriptor1 = &remap_info->remap_descriptor_test;
        ide_drive_t *drive;
        struct gendisk *disk;

	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &ide_hwifs[prm_scd].drives[mst_slv];
	disk = ((struct ide_disk_obj *)drive->driver_data)->disk;

        /*
         * if we check this disk had invalid signature,
         * we should create remap signature and create a
         * remap table
         */
        if ((ret = check_remap_signature(disknum))) {
                if (ret == -1 ){
#ifdef ACS_DEBUG
                        printk("load_table():error in check remap signature!\n");
#endif
                        return -1;
                }
                if (create_remap_signature(disknum)){
#ifdef ACS_DEBUG
                        printk("load_remap_table()->create_remap_signature() FAILED\n");
#endif
                        return -1;
                }
                if (create_remap_table(disknum)){
#ifdef ACS_DEBUG
                        printk("load_remap_table()->create_remap_table() FAILED\n");
#endif
                        return -1;
                }
                return 0;
        }

        ret = rw_remap_sector(disknum, buf, WIN_READ, REMAP_DESCRIPTOR_ADDRESS);
        if(ret == -1){
#ifdef ACS_DEBUG
                printk("load_table():rw remap sector error, maybe part is err!\n");
#endif
                return ret;
        }
        /*** Sunny 2005.12.12 begin  Disk structure is 2.6.12 ***/
        memcpy(remap_descriptor, buf, sizeof(remap_descriptor_t));
        if(buf[0] == 0){
                memcpy(remap_descriptor1, buf, sizeof(remap_descriptor_t));
                if(remap_descriptor1->start_sec == 0x1022){     /* Bob 2006.9.14 */
                        remap_descriptor->start_sec = remap_descriptor1->start_sec + disk->part[2]->start_sect + drive->sect0;
                        remap_descriptor->end_sec = remap_descriptor1->end_sec + disk->part[2]->start_sect + drive->sect0;
                }
                remap_descriptor->count = remap_descriptor1->count << 16;
                remap_descriptor->flag = 1;
                remap_descriptor->check_sum = remap_descriptor->start_sec + remap_descriptor->end_sec + remap_descriptor->count;
        }

        ret = rw_remap_sector(disknum, (char*)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        if(ret == -1){
#ifdef ACS_DEBUG
                printk("load_table():write remap_ descriptor error, maybe part is err!\n");
#endif
                return ret;
        }
        /*** Sunny 2005.12.12 end ***/

        if (remap_descriptor->count >= BLOCK_REMAP_LIMIT){
                printk(KERN_EMERG "Disk %d Too Many Bad Sector Counts %lu\n", disknum, remap_descriptor->count);
                write_kernellog("%d Disk %d Too Many Bad Sector Counts %lu", ERROR_LOG, disknum, remap_descriptor->count);
                set_disk_fail(disknum);
                return -1;
        }

        if (remap_descriptor->count >= BLOCK_REMAP_WARN){
                printk(KERN_EMERG "Disk %d Bad Sector Counts %lu\n", disknum, remap_descriptor->count);
                write_kernellog("%d Disk %d Bad Sector Counts %lu", WARN_LOG, disknum, remap_descriptor->count);
        }

        remap_info->stat = REMAP_TABLE_VALID;

        if (remap_descriptor->count){
                if (my_read_remap_table(disknum, remap_descriptor->count, disk, drive)){
                       return -1;
                }
        }

        return 0;
}

/**
 * Function: 	create_remap_table()
 * Description: create disk remap table and write to disk 
 * while disk initialized or hotswap
 * Input:
 * return:	0 on success, otherwise -1
 */
int create_remap_table(unsigned char disknum)
{
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;

#ifdef  ACS_DEBUG
        acs_printk("%s: disknum = %d\n", __func__, disknum);
#endif

        if (init_block_remap(disknum)){
#ifdef  ACS_DEBUG
                acs_printk("%s: remap_table has already been valid!\n", __func__);
#endif
                return -1;
        }
        remap_info->stat = REMAP_TABLE_VALID;
        rw_remap_sector(disknum, (char*)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        return 0;
}

#if	0
/*** Sunny 2005.12.17 begin ***/
int create_remap_table_test(int index)
{
        remap_info_t *remap_info = &remap_infos[index];
        remap_descriptor_test_t *remap_descriptor_test = &remap_info->remap_descriptor_test;

        remap_info->stat = REMAP_TABLE_VALID;
        rw_remap_sector(index,(char*)remap_descriptor_test,WIN_WRITE,REMAP_DESCRIPTOR_ADDRESS);
        return 0;
}
/*** Sunny 2005.12.17 end ***/
#endif

unsigned long find_block_remap_entry(block_remap_t **, unsigned int, int);
/**
 * Function:    get_remap_entry()
 * Description: get a free remap entry for remap a bad block
 * Input:       disknum, block_remap_t, sector
 * return:      0 on success, otherwise -1
 */
int 
grow_remap_entry(unsigned char disknum, sector_t sec, int len, block_remap_t **block_remap)
{
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;

#ifdef ACS_DEBUG
        acs_printk("%s: disknum = %d, sector = %lu \n", __func__, disknum, sec);
#endif

        *block_remap = kmalloc(sizeof(block_remap_t), GFP_ATOMIC);
        (*block_remap)->remap.old_sec = sec;
        (*block_remap)->remap.new_sec = remap_descriptor->end_sec;
        (*block_remap)->remap.len = len;
        if(insert_remap_table(disknum, *block_remap)){
#ifdef  ACS_DEBUG
        	acs_printk("%s: failed!\n", __func__);
#endif
                return -1;
        }

        remap_descriptor->flag = 1;
        remap_descriptor->end_sec += len; /**** pio length ****/
        remap_descriptor->count++;
        remap_descriptor->check_sum = remap_descriptor->start_sec+remap_descriptor->end_sec+ remap_descriptor->count;
        return 0;
}

/**
 * Function:    show_remap_table_info()
 * Description: to show infomation about remap_tables[]
 * Input:       None
 * Return:      None
 */
void show_remap_table_info(void){
       int i;
       for (i=0; i < DISK_NUM; i++){
                remap_info_t *remap_info = &remap_infos[i];
                remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
                printk("index = %d, stat = %lu, start_sec = %lu, end_sec = %lu, count = %lu\n", i, remap_info->stat, remap_descriptor->start_sec, remap_descriptor->end_sec, remap_descriptor->count);
	}
}

/**
 * Function:    show_block_remap_entry()
 * Description: to show information about block_remap[]
 * Input:       disknum
 * Return:      None
 */
#if	0
void show_block_remap_entry(unsigned char disknum)
{
        remap_info_t *remap_info = &remap_infos[disknum];
        //remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
        //unsigned long count = remap_descriptor->count;
        int i = 0;

        if (remap_info->stat == REMAP_TABLE_INVALID){
                printk("remap table didn't contain valid data!\n");
                return;
        }

        for (i = 0; i < 64; i++){
                block_remap_t *block_remap = &remap_info->block_remap[i];
                block_remap_t *next;
                if (block_remap->no > 0){
                        printk("block_remap entry index = %d, no = %lu\n", i, block_remap->no);
                        next = block_remap->next;
                        while (next != block_remap){
                                remap_entry_t *remap = &next->remap;
                                next = next->next;
                        }
                }
        }
}
#endif

/**
 * Function:    bad_blk_remap_init
 * description: create remap table and write it to disk while adding disk
 * Input:       drive
 * return:	0 on success, otherwise -1
 */
int bad_blk_remap_init(unsigned char disknum)
{
	unsigned char prm_scd, mst_slv;
        ide_drive_t *drive;

#ifdef  ACS_DEBUG
        acs_printk("%s: disknum = %d\n", __func__, disknum);
#endif

	get_disk_intf(disknum, &prm_scd, &mst_slv);
        drive = &ide_hwifs[prm_scd].drives[mst_slv];
#ifdef	ACS_DEBUG
	if (!drive)
		acs_printk("%s: !drive\n", __func__);
	if (!drive->queue)
		acs_printk("%s: !drive->queue\n", __func__);
#endif
        drive->queue->make_request_fn = accu_make_request;
	acs_printk("%s: init_block_remap\n", __func__);
        if (init_block_remap(disknum)){
#ifdef  ACS_DEBUG
                acs_printk("%s: init block remap Fail!\n", __func__);
#endif
                return -1;
        }
        return (load_remap_table(disknum));
}

#if	0
/*** Sunny 2005.12.17 begin ***/
int ide_add_remap_table_test(unsigned int index){
        int i;
        remap_info_t *remap_info = &remap_infos[index];
        remap_descriptor_test_t *remap_descriptor_test = &remap_info->remap_descriptor_test;
        remap_table_t *remap_table_temp = kmalloc(sizeof(remap_table_t) * 512, GFP_ATOMIC);
        if (init_block_remap_test(index, remap_descriptor_test)){
                printk("ide_add_remap_table(): init_block_remap_test() Failed!\n");
                return -1;
        }
        if (create_remap_signature(index)){
                        printk("load_remap_table()->create_remap_signature() FAILED\n");
                        return -1;
        }


        memset(remap_table_temp, 0, sizeof(remap_table_t)*512);
        for (i=0; i < 33; i++){
                remap_table_temp[i].old_sec = i * 10 + 1;
                remap_table_temp[i].new_sec = remap_descriptor_test->end_sec;
                remap_descriptor_test->end_sec += 8;
                remap_descriptor_test->count += 1;
                remap_table_temp[i].part = 4;
        }

        remap_info->stat = REMAP_TABLE_VALID;
        if (rw_remap_sector(index,(char*)remap_descriptor_test,WIN_WRITE,REMAP_DESCRIPTOR_ADDRESS)){
                        return -1;
        }

        for (i = 0; i < 16; i++){
                if (rw_remap_sector(index, (char *)&remap_table_temp[32*i], WIN_WRITE, (REMAP_TABLE_ADDRESS+i))){
                        return -1;
                }
        }

        return 0;
}
/*** Sunny 2005.12.17 end ***/
#endif

/**
 * Function:    ide_remove_remap_table()
 * Description: remove table and free block_remap memory
 * Input:       disknum
 * return:      None
 */
int ide_remove_remap_table(unsigned char disknum){
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
        block_remap_t *block_remap = NULL;
        int i =0;

#ifdef  ACS_DEBUG
        acs_printk("ide_remove_remap_table(): disknum = %d\n", disknum);
#endif
	set_remap_table_invalid(disknum);
        if (!remap_descriptor->count)
                return 0;

        for (i = 0; i<64; i ++){
                block_remap_t *next;
                block_remap = &remap_info->block_remap[i];
                next = block_remap->next;
                while (block_remap->no > 0){
                        block_remap->next = next->next;
                        kfree(next);
                        next = block_remap->next;
                        block_remap->no--;
                }
        }

        return 0;
}
extern int md_do_remap(struct bio *, int);

/**
 * Function: 	bad_block_remap_write()
 * Description: remap bad block
 * Input:
 * return: ??
 */
int bad_block_remap_write(void *p)
{
        struct bio *bio = (struct bio *)p;
        int i = 0, j = 0, minor = MINOR(bio->bi_bdev->bd_dev);
        uint64_t mod_no;
        unsigned long flags;
        sector_t start_sec[4], org_sector = bio->bi_sector;
        block_remap_t *block_remap = NULL;
        remap_entry_t *remap = NULL;
	unsigned char disknum = get_disknum_by_dev(bio->bi_bdev->bd_dev), prm_scd, mst_slv;
        int block_nr = bio->bi_size >> 9;
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
        ide_drive_t *drive;
        struct gendisk *disk;

	
        remap_entry_t *remap_entry_temp = kmalloc(sizeof(remap_entry_t) * 512, GFP_ATOMIC);
        if(!remap_entry_temp){
                printk("alloc remap_entry_temp fail!\n");
                return -ENOMEM;
        }

	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &(ide_hwifs[prm_scd].drives[mst_slv]);
	disk = ((struct ide_disk_obj*)(drive->driver_data))->disk;

        for(i = 0; i < 4; i++){
                start_sec[i] = disk->part[i]->start_sect;
        }

        if((org_sector >= start_sec[0]) && (org_sector < start_sec[1]))
                minor = 1;
        else if((org_sector >= start_sec[1] && (org_sector < start_sec[2])))
                minor = 2;
        else if((org_sector >= start_sec[2] && (org_sector < start_sec[3])))
                minor = 3;
        else if(org_sector >= start_sec[3])
                minor = 4;

        local_save_flags(flags);
        local_irq_disable();

        if (drive_stat[disknum] & IDE_OFFLINE){
                local_irq_restore(flags);
                printk("%s: Disk %d is pullout\n", __func__, disknum);
                kfree(remap_entry_temp);
                return -1;
        }

        if (minor == 3) {
                local_irq_restore(flags);
                printk("%s: minor is %d\n", __func__, minor);
                goto REMAP_CRITICAL_FAIL;
        }

#if 0
        /* Sunny 2006.03.23 begin */
        count = remap_descriptor->count;
        count_high = count >> 16;
        if(count_high * 8 % 256 ==0)
                count_temp = (count_high * 8 /256) + (count & 0xffff);
        else
                count_temp = (count_high * 8 /256) +1 + (count & 0xffff);
        /* Sunny 2006.03.23 end */
#endif

        if (remap_descriptor->count >= BLOCK_REMAP_LIMIT){
                local_irq_restore(flags);
                goto REMAP_SET_FAIL;
        }

        mod_no = 0;
        block_remap = &remap_info->block_remap[mod_no];
        block_remap = NULL;

        if (grow_remap_entry(disknum, org_sector, block_nr, &block_remap)){
                local_irq_restore(flags);
                goto REMAP_FAIL;
        }
        remap_write_count++;
        remap = &(block_remap->remap);

        for (i = 0; i < 64; i++){
                block_remap_t *head = &remap_info->block_remap[i];
                block_remap_t *next = head->next;
                while (next != head){
                        memcpy(&remap_entry_temp[j], &next->remap, sizeof(remap_entry_t));
                        next = next->next;
                        j++;
                }
        }

        local_irq_restore(flags);
        for (i = 0; i < 16; i++){
                if (rw_remap_sector(disknum, (char *)&remap_entry_temp[32*i], WIN_WRITE, (REMAP_TABLE_ADDRESS+i))){
                        goto REMAP_FAIL;
                }
        }
        if (rw_remap_sector(disknum, (char *)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS)){
                goto REMAP_FAIL;
        }
        kfree(remap_entry_temp);
        generic_make_request(bio);

        /* Bob 2006.9.14 --start */
        if (remap_descriptor->count >= BLOCK_REMAP_WARN){
                printk(KERN_EMERG "Disk %d Bad Sector Counts %lu\n", disknum, remap_descriptor->count);
                write_kernellog("%d Disk %d Bad Sector Counts %lu", WARN_LOG, disknum, remap_descriptor->count);
        } else{
                write_kernellog("%d Disk %d Bad Sector Counts %lu", WARN_LOG, disknum, remap_descriptor->count);
        }
        /* Bob 2006.9.14 --end */

	remap_do_request(prm_scd, mst_slv);

        return 0;

REMAP_CRITICAL_FAIL:
        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT;//Sunny 2005.12.12
        kfree(remap_entry_temp);
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        rw_remap_sector(disknum, (char *)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return -1;

REMAP_FAIL:
        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT + 1;
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        kfree(remap_entry_temp);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return -1;

REMAP_SET_FAIL:

        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT + 1;
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        kfree(remap_entry_temp);
        kfree(remap_entry_temp);
        rw_remap_sector(disknum, (char *)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return -1;
}

/******************************************************************************
 * Input:
 * return: ??
 ******************************************************************************/
int bad_block_remap_read(void *p)
{
        struct bio *bio = (struct bio *)p;
        struct bio_vec *bvec;
        int i = 0, j = 0, ret, minor = MINOR(bio->bi_bdev->bd_dev);
        uint64_t mod_no;
        unsigned long sector, flags;
        sector_t start_sec[4];
	unsigned char disknum = get_disknum_by_dev(bio->bi_bdev->bd_dev), prm_scd, mst_slv;
        sector_t org_sector = bio->bi_sector;
	ide_drive_t *drive;
        struct gendisk *disk;
        char *data,*data1;
        remap_info_t *remap_info = &remap_infos[disknum];
        remap_descriptor_t *remap_descriptor = &remap_info->remap_descriptor;
        remap_entry_t *remap_entry_temp, *remap_table = NULL;
        block_remap_t *block_remap   = NULL;

        remap_entry_temp = kmalloc(sizeof(remap_entry_t) * 512, GFP_ATOMIC);
        if(!remap_entry_temp){
                printk("alloc in remap read fail!\n");
                return -ENOMEM;
        }

	get_disk_intf(disknum, &prm_scd, &mst_slv);
	drive = &(ide_hwifs[prm_scd].drives[mst_slv]);
	disk = ((struct ide_disk_obj*)(drive->driver_data))->disk;

        for(i = 0; i < 4; i++){
                start_sec[i] = disk->part[i]->start_sect;
        }

        if((org_sector >= start_sec[0]) && (org_sector < start_sec[1]))
                minor = 1;
        else if((org_sector >= start_sec[1] && (org_sector < start_sec[2])))
                minor = 2;
        else if((org_sector >= start_sec[2] && (org_sector < start_sec[3])))
                minor = 3;
        else if(org_sector >= start_sec[3])
                minor = 4;

#ifdef  ACS_DEBUG
        acs_printk("%s: disknum = %d, minor = %d\n", __func__, disknum, minor);
#endif

        local_save_flags(flags);
        local_irq_disable();

        if (drive_stat[disknum] & IDE_OFFLINE){
                local_irq_restore(flags);
                printk("%s: Disk:%d is pollout\n", __func__, disknum);
                local_irq_restore(flags);
                kfree(remap_entry_temp);
                return -1;
        }

        if (minor == 3) {
                local_irq_restore(flags);
                kfree(remap_entry_temp);
                goto REMAP_CRITICAL_FAIL;
        }

        if (remap_descriptor->count >= BLOCK_REMAP_LIMIT){
                local_irq_restore(flags);
                kfree(remap_entry_temp);
                goto REMAP_SET_FAIL;
        }
        ret = md_do_remap(bio,1);
        if (ret == -1){
                kfree(remap_entry_temp);
                bio_endio(bio, bio->bi_size, -EIO);
                remap_do_request(prm_scd, mst_slv);
                local_irq_restore(flags);
                return 0;
        }

        mod_no = 0;
        block_remap = &remap_info->block_remap[mod_no];

        if (grow_remap_entry(disknum, org_sector, (bio->bi_size>>9), &block_remap)){
                local_irq_restore(flags);
                kfree(remap_entry_temp);
                goto REMAP_FAIL;
        }
        remap_read_count++;
        remap_table = &(block_remap->remap);

#ifdef  ACS_DEBUG
        acs_printk("%s: grow_remap_entry() successful! new_block = 0x%lx\n", __func__, remap_table->new_sec);
#endif

        for (i = 0; i < 64; i++){
                block_remap_t *head = &remap_info->block_remap[i];
                block_remap_t *next = head->next;
                while (next != head){
                        memcpy(&remap_entry_temp[j], &next->remap, 16);
                        next = next->next;
                        j++;
                }
        }

        local_irq_restore(flags);

        for (i = 0; i < 16; i++){
                if (rw_remap_sector(disknum, (char*)&remap_entry_temp[32*i], WIN_WRITE, (REMAP_TABLE_ADDRESS+i))){
                        kfree(remap_entry_temp);
                        goto REMAP_FAIL;
                }
        }

        if (rw_remap_sector(disknum, (char*)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS)){
                kfree(remap_entry_temp);
                goto REMAP_FAIL;
        }
        kfree(remap_entry_temp);
        sector = remap_table->new_sec;
        sector = sector - disk->part[2]->start_sect - drive->sect0;
//      struct page *pages[32];
//      for(i = bio->bi_idx; i < bio->bi_vcnt; i++)

//      data = vmap(pages, bio->bi_vcnt, VM_MAP, PAGE_KERNEL);
        data = kmalloc(bio->bi_size, GFP_NOIO);
        if(!data){
                printk(KERN_EMERG "alloc data temp in remap read fail!\n");
                return -1;
        }
        data1 = data;
        for(i = bio->bi_idx; i < bio->bi_vcnt; i++){
                bvec  =  &bio->bi_io_vec[i];
                memcpy(data, page_address(bvec->bv_page) + bvec->bv_offset, bvec->bv_len);
                data += bvec->bv_len;
        }
        data=data1;
//      memcpy(data_temp,bio_data(bio), bio->bi_size);
        for (i = 0; i < bio->bi_size / 512; i++){
                if (rw_remap_sector(disknum, (char*)data1, WIN_WRITE, (sector + i))){
                        printk(KERN_EMERG "emap_read()->rw_remap_sector: Failed, i %d\n", i);
                        kfree(data);
                        goto REMAP_FAIL;
                }
                data1+=512;
        }
        kfree(data);
//      vunmap(data);
//      for(i = bio->bi_idx; i < bio->bi_vcnt; i++)
//              __free_pages(pages[i], 0);
        bio->bi_rw |= (1<<12);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return 0;

REMAP_CRITICAL_FAIL:
        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT;
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        rw_remap_sector(disknum, (char*)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return -1;

REMAP_FAIL:
        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT + 1;
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);
        return -1;

REMAP_SET_FAIL:
        remap_descriptor->count = (remap_descriptor->count & 0xffff0000) + BLOCK_REMAP_LIMIT + 1;
        remap_descriptor->check_sum = remap_descriptor->count + remap_descriptor->start_sec + remap_descriptor->end_sec;
        rw_remap_sector(disknum, (char*)remap_descriptor, WIN_WRITE, REMAP_DESCRIPTOR_ADDRESS);
        generic_make_request(bio);
	remap_do_request(prm_scd, mst_slv);

        return -1;
}
#endif	//BAD_BLK_REMAP
