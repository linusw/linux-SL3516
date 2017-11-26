#ifndef	_BAD_BLK_REMAP_H
#define	_BAD_BLK_REMAP_H


#define BLOCK_REMAP_LIMIT 512
#define BLOCK_REMAP_WARN 384

typedef struct remap_entry_s {	/* 16 bytes */
	sector_t	old_sec;
	unsigned long  new_sec;
	unsigned long len;
} remap_entry_t;

/* remap block state */
#define	BLOCK_VALID	0x00
#define	BLOCK_INVALID	0x01
#define BLOCK_HEAD      0xFF

#define	BADSEC_MAGIC	0x76F13D5E

#define	REMAP_OFFSET	0
#define	REMAP_UNIT	PAGE_SIZE	/* 4K */

typedef struct block_remap_s {
	struct block_remap_s *next;
	unsigned long no;    /*** test ***/
	remap_entry_t remap;
} block_remap_t;

#define REMAP_TABLE_VALID    0x01
#define REMAP_TABLE_INVALID  0x00

typedef struct remap_descriptor_s {
	unsigned short flag;/*** Sunny 2005.12.12 ***/
	unsigned long start_sec;
	unsigned long end_sec;
	unsigned long count;
	unsigned long long check_sum;
} remap_descriptor_t;


/*** Sunny 2005.12.17 begin ***/
typedef struct remap_descriptor_test_s {
	unsigned char rev1;
	unsigned char rev2;
	unsigned long start_sec;
	unsigned long end_sec;
	unsigned long count;
} remap_descriptor_test_t;

typedef struct remap_table_s {	/* 16 bytes */
	unsigned long	old_sec;
	unsigned long	new_sec;
	unsigned short	stat;
	unsigned char	part;
	unsigned char	version;
	unsigned long	rev2;
} remap_table_t;

typedef struct block_remap_test_s {
	struct block_remap_test_s *next;
	unsigned char no;    /*** test ***/
	remap_table_t remap_table;
} block_remap_test_t;

 
typedef struct remap_info_s {
	unsigned long stat;
	remap_descriptor_t remap_descriptor;	
	block_remap_t block_remap[64];
	remap_descriptor_test_t remap_descriptor_test;/*** Sunny 2005.12.17 ***/
	block_remap_test_t block_remap_test;/*** Sunny 2005.12.17 ***/
} remap_info_t;

/*** Sunny 2005.12.17 end ***/

#define REMAP_SIGNATURE_ADDRESS   0x1000
#define REMAP_DESCRIPTOR_ADDRESS  0x1001
#define REMAP_TABLE_ADDRESS       0x1002
#define REMAPPED_START_ADDRESS    0x1022
#define REMAPPED_END_ADDRESS  	  (REMAPPED_START_ADDRESS + 0x1000)	

#define REMAP_SPECIAL 3

#define TEST_MINOR	4
#define TEST_INDEX 	0

typedef struct vbio_endio
{
	int io_remaining;
	struct bio *p_bio;
	int error;
}vbio_endio_t;


typedef struct extent_s 
{
	sector_t  start;
	sector_t  end;
}extent_t;


vbio_endio_t* endio_alloc(void);

void endio_free(vbio_endio_t* vbio);

int snapshot_bio_endio(struct bio *bio, unsigned int arg, int error);

int 
bio_remap_check(struct bio *bio, extent_t *ext, block_remap_t *blk_remap, struct bio **head, vbio_endio_t *endio);

//int 
//make_bio(struct bio *bio, struct bio **head, uint64_t start, uint64_t end, remap_entry_t *entry, vbio_endio_t *endio);

void
intersection(extent_t *ext, remap_entry_t *entry, extent_t *ret);

//static void 
//snapshot_bio_map(struct bio *bio, int done_sector, struct bio *pbio, int bsize);

int accu_make_request(request_queue_t *q, struct bio *bio);

void init_vbio(void);
void exit_vbio(void);

extern remap_info_t remap_infos[];
extern wait_queue_head_t remap_queue;
extern int work_daemon(void);
typedef int (thread_fn)(void*);
typedef struct work_remap_s {
        thread_fn *p_fn;
        struct bio *bio;
        struct list_head list;
} work_remap_t;
extern spinlock_t remap_lock;
extern struct list_head work_list;

extern int bad_blk_remap_init(unsigned char);
extern int rw_remap_sector(unsigned char, unsigned char*, unsigned char, unsigned long);
extern int bad_block_remap_read(void *);
extern int bad_block_remap_write(void *);
extern void remap_do_request(unsigned char, unsigned char);
extern int bad_blk_remap_init(unsigned char);

#endif	//_BAD_BLK_REMAP_H
