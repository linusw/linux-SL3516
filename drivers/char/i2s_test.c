#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <syslog.h>

#include <termios.h>
#include <unistd.h>
#include <assert.h>
//#include <asm/arch/gemini_i2s.h>
#include </source/kernel/linux/include/asm/arch/gemini_i2s.h>

#include <errno.h>
#define MENU_NUM  2
#define SBUF_SIZE  2048//1024 



// mknod /dev/sspi2s c 10 244
int main(int argc, char *argv[])
{
    int fd;
    FILE *fp;
    char imgPt[] = "/dev/sspi2s";
    char filename[] = "/mnt/ide1/public/test.wav";
    int i, inprog=1, err, j, k, outfd, *tmp=0, *tmpo=0, ret, timeout, ch;
    unsigned int value;
    struct stat fileStat;
    int handle;
    char *imgBuff;
    int fileLen;
    //int ifunc, garbage;
    char ifunc, garbage;
    //unsigned char *buf = NULL, *obuf = NULL, *tbuf = NULL;
    char menu[][30] = {
    						{"0. Play test"},
    						{"Q. Exit"},

    						
    };
    
            printf("MESSAGE: Run SSP Program now...\n");
           usleep(10000);
         
        if( (fd = open(imgPt, O_SYNC|O_RDWR)) < 0)
        {
            printf("ERROR: Open I2S device error: \n");
            return -1;
        }

		printf("MESSAGE: Open SLIC device successfully: \n");
  			
  		while(inprog)
  		{	
  			printf("\n\n ======================= SSP I2S Test =======================\n");

  			  
  			for(i=0;i<(MENU_NUM/2);i++)  
  			{
  				printf("%-30s        %-30s\n", &menu[2*i][0],&menu[2*i+1][0]);
  			}
  			if(MENU_NUM%2)
  				printf("%-30s        \n", &menu[MENU_NUM-1][0]);
  			  //ifunc = getchar();
			//printf("--> %d \n",ifunc);
			do {
				printf("===>");
  			  scanf("%c",&ifunc);
  			  //printf("><%c\n",ifunc);
  			  scanf("%c", &garbage);
		      if (garbage != '\n')
		      	continue;
			} while (((ifunc < '0') || (ifunc > '9')) && 
			 ((ifunc < 'a') || (ifunc > 'z')) &&
			 ((ifunc < 'A') || (ifunc > 'Z')));
			
			switch(ifunc)
			{
				case '0':
					ioctl( fd, SSP_I2S_INIT_BUF, NULL);
					if((fp = fopen(filename, "rb")) == NULL)
					{
						printf("open file error: %s\n",filename);
	 					break;
	 				}
	 				
	 				handle = fileno(fp);
        if (fstat(handle,&fileStat) < 0)
        { 
            printf("read file state error\n");
	 					break;
        }
        //Get file Length
        fileLen = fileStat.st_size;
        printf("fileLen = %d (0x%x)\n",fileLen,fileLen);
        
        ioctl(fd, SSP_I2S_FILE_LEN, &fileLen);
        
        imgBuff = malloc(SBUF_SIZE); //malloc(fileLen+2);
    		if (! imgBuff)
    		{      
        	printf("imgBuff buffer alloc error.\n");
	 					break;
    		}

				fseek(fp,0,0);
     		
     		i = fileLen / SBUF_SIZE;
     		j = fileLen % SBUF_SIZE;
     		printf("I2S test start --> i:%d  j:%d\n",i,j);
     		for(k=0;k<i;k++)
     		{
     			memset(imgBuff, 0x0 ,SBUF_SIZE);
     		   fseek(fp,k*SBUF_SIZE,SEEK_SET); //SEEK_SET =0 //beginning of file
     		   fread(imgBuff,SBUF_SIZE,1,fp);          
wt_again:     		   			
     		   			err = write (fd,imgBuff,SBUF_SIZE);
	 							if (err < 0)
	 							{
									printf("write err !!\n");
									break;
	 							}
	 						//	printf("len = %d\n",err);
	 							if( err != SBUF_SIZE)
	 							{
	 								//printf("no empty buffer !!\n");
	 								
	 								goto wt_again;
	 							}
	 							//printf(".");
     		}
     		if(j!=0)
     		{
     				memset(imgBuff, 0x0 ,SBUF_SIZE);
     		   	fseek(fp,k*SBUF_SIZE,SEEK_SET); //SEEK_SET =0 //beginning of file
     		   	fread(imgBuff,j,1,fp);          
wt_again1:     		   			
     		   			err = write (fd,imgBuff,SBUF_SIZE);
	 							if (err < 0)
	 							{
									printf("write err !!\n");
									break;
	 							}
	 							if( err != SBUF_SIZE)
	 							{
	 								//printf("no empty buffer !!\n");
	 								goto wt_again1;
	 							}
	 							//printf("*");
     			
     		}
     		printf("\nI2S test end <--\n");
     		ioctl( fd, SSP_I2S_STOP_DMA, NULL);
     		
//Read file to buffer               
        
/*
�W�@�١Gfseek
�y�@�k�Gint fseek(int fp, int offset);
�Ǧ^�ȡG���
²�@���G�����ɮ׫��СC
�ء@���G�ɮצs��

���禡�N�ɮ� fp �����в�����w�������줸 (offset) �W�C�ϥΥ��禡�N�� C �y������ fseek(fp, offset, SEEK_SET) �禡�C���\�h�Ǧ^ 0�A���ѫh�Ǧ^ -1 �ȡC�� fp �� fopen() �}�� "http://...." �άO "ftp://...." �� URL �ɮ׮ɡA���禡�L�k�@�ΡC


--------------------------------------------------------------------------------

�W�@�١Gftell
�y�@�k�Gint ftell(int fp);
�Ǧ^�ȡG���
²�@���G���o�ɮ�Ū�g���Ц�m�C
�ء@���G�ɮצs��

���禡�Ǧ^�ɮ� fp �����а����줸 (offset) �ȡC��o�Ϳ��~�ɡA�Ǧ^ false �ȡC�ɮ׫��� fp �����O���Ī��A�B�ϥ� fopen() �Ϊ� popen() �G�Ө禡�}�Ҥ�i�@�ΡC
*/        
				printf("close file ctrl \n");
        fclose(fp);  
 						 							
					break;
				case 'Q':
					printf(" Q. Exit\n");
					inprog = 0;
					ioctl( fd, SSP_I2S_STOP_DMA, NULL);
					break;
				default:
					printf("Out of Range !!\n");
				
			}//switch...
	  		
      	}//while...

				printf("close phone dev  !!\n");
      	close(fd);      

        printf("MESSAGE: Exit SLIC test program!!\n\n");
          usleep(10000); 
    return 0;
}
