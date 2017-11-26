#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* for hotswap ioctl */
#define HS_GET_HOTSWAP          0x0001
#define GET_NAS_MODEL           0x0002
#define GET_DISK_STAT           0x0003
#define ADD_DISK_TEST           0x0004
#define REMOVE_DISK_TEST        0x0005
#define RESET_DISK_TEST         0x0006
#define GET_IO_SIZE_COUNT       0x0007
#define GET_IDE_STAT		0x0008
#define GET_HOTSWAP_STAT        0x0009
#define CLEAR_HOTSWAP_STAT      0x000A

#define	HOTSWAP	"/dev/hotswap"
#define mkdev(maj, min) (((maj) << 8) | (min))

void Usage(void){
	printf("Usage: Command operation disk_num or Command s\n");
	printf("\toperation: ad:add disk, rm:remove disk, rs:reset disk\n");
}

int main(int argc, char **argv){
	int fd, ret = -1, arg, retry = 0;
	char *ops;
	unsigned char args[2];

	if(argc == 2 && argv[1][0] != 's' && argc != 3){	
		printf("argc=%d\n", argc);
		Usage();
		return ret;
	}

	ops = argv[1];
	if(argc == 3){
		if(*ops != 'a' && *ops != 'r'){
			printf("operation=%c\n", *ops);
			Usage();
			return ret;
		}
		arg = atoi((const char*)argv[2]);
		if(arg < 1 || arg > 4){
			printf("disk_num=%d\n", arg);
                	Usage();
                	return ret;
		}
	} else {
		if(*ops != 's'){
			printf("operation=%c\n", *ops);
			Usage();
			return ret;
		}
	}

open_retry:
	fd = open(HOTSWAP, O_RDWR);
	if(fd <= 0){
		printf("ERROR: open fail!\n");
		if(retry == 0){
			printf("retry ...\n");
			ret = mknod("/dev/hotswap", S_IFCHR | S_IRUSR | S_IWUSR, mkdev(10, 242));
			if(ret){
				printf("ERROR: mknod fail!\n");
				return ret;
			}
			retry = 1;
			goto open_retry;
		}
		return ret;
	}

	if (*ops == 'a' && *(ops + 1) == 'd') {
		ret = ioctl(fd, ADD_DISK_TEST, &arg);
	} else if (*ops == 'r' && *(ops + 1) == 'm') {
		ret = ioctl(fd, REMOVE_DISK_TEST, &arg);
	} else if (*ops == 'r' && *(ops + 1) == 's') {
		ret = ioctl(fd, RESET_DISK_TEST, &arg);
	} else if (*ops == 's') {
		ret = ioctl(fd, GET_IDE_STAT, args);
		if(!ret)
			printf("Ide status register is %x %x\n", args[0], args[1]);
	} else {
		printf("ERROR: fail!\n");
		return -1;
	}

	if (ret) {
		printf("ERROR: ioctl fail!\n");
                return ret;
	}
}
