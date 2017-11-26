#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <syslog.h>
//#include <linux/telephony/gemini.h>
//#include <linux/telephony/proslic.h>
//#include </home/middle/lepus/source/kernel/linux/include/linux/telephony.h>
#include <linux/telephony.h>

#include <errno.h>
#define MENU_NUM  13
// mknod /dev/phone0 c 100 0
int main(int argc, char *argv[])
{
    int fd;
    char imgPt[] = "/dev/phone0";
    int i, inprog=1;
    unsigned int value;
    int ifunc, garbage;
    char menu[][30] = {
    						{"0. Busy Tone"},
    						{"1. Ring Back Tone"},
    						{"2. OFF HOOK Tone"},
    						{"3. Congestion Tone"},
    						{"4. Ringing"},
    						{"5. Stop Ringing"},
    						{"6. Stop Tone"},
    						{"7. Hook States"},
    						{"8. Get Linefeed states"},
    						{"9. Set Linefeed states"},
    						{"a. Test phone transation"},
    						{"b. Test phone DTMF"},
    						{"Q. Exit"},

    						
    };
    
            printf("MESSAGE: Run SSP Program now...\n");
           usleep(10000);
         
        if( (fd = open(imgPt, O_SYNC|O_RDWR)) < 0)
        {
            printf("ERROR: Open SLIC device error: \n");
            return -1;
        }

		printf("MESSAGE: Open SLIC device successfully: \n");
  			
  		while(inprog)
  		{	
  			printf("\n\n ======================= SSP Test =======================\n");
  		//	  printf(" 1. Busy Tone\n");
  		//	  printf(" 2. Ring Back Tone\n");
  		//	  printf(" 3. OFF HOOK Tone\n");
  		//	  printf(" 4. Congestion Tone\n");
  		//	  printf(" 5. Ringing\n");
  		//	  printf(" 6. Stop Ringing\n");
  		//	  printf(" 7. Stop Tone\n");
  		//	  printf(" 9. Exit\n");
  		//	  //printf(" 6. Dump SSP Registers\n");
  		//	  printf("===> ");
  		//	  scanf("%d",&ifunc);
  			  
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
  			  scanf("%c", &garbage);
		      if (garbage != '\n')
		      	continue;
			} while (((ifunc < '0') || (ifunc > '9')) && 
			 ((ifunc < 'a') || (ifunc > 'z')) &&
			 ((ifunc < 'A') || (ifunc > 'Z')));
			
			switch(ifunc)
			{
				case '0':
					printf(" 0. Busy Tone\n");
					ioctl( fd, SSP_GEN_BUSY_TONE, NULL);
					break;
				case '1':
					printf(" 1. Ring Back Tone\n");
					ioctl( fd, SSP_GEN_RINGBACK_TONE, NULL);
					break;
				case '2':
					printf(" 2. OFF HOOK Tone\n");
					ioctl( fd, SSP_GEN_OFFHOOK_TONE, NULL);
					break;
				case '3':
					printf(" 3. Congestion Tone\n");
					ioctl( fd, SSP_GEN_CONGESTION_TONE, NULL);
					break;
				case '4':
					printf(" 4. Ringing\n");
					ioctl( fd, SSP_PHONE_RING_START, NULL);
					break;
				case '5':
					printf(" 5. Stop Ringing\n");
					ioctl( fd, SSP_PHONE_RING_STOP, NULL);
					break;
				case '6':
					printf(" 6. Stop Tone\n");
					ioctl( fd, SSP_DISABLE_DIALTONE, NULL);
					break;
				case '7':
					printf(" 7. Hook states\n");
					value = 0;
					ioctl( fd, SSP_GET_HOOK_STATUS, &value);
					if(value==0)
						printf("    ---->   Off Hook.\n");
					else
						printf("    ---->   On Hook.\n");
					break;	
				case '8':
					printf(" 8. Get Linefeed states\n");
	//0x00	Open
	//0x11	Forward active
	//0x22	Forward on-hook transmission
	//0x33	TIP open
	//0x44	Ringing
	//0x55	Reverse active
	//0x66	Reverse on-hook transmission
	//0x77	RING open
					value = 0;
					ioctl( fd, SSP_GET_LINEFEED, &value);
					printf("Linefeed : 0x%x\n",value);
						if(value&0x0f==0)
							printf("Linefeed : Open\n");
						else if(value&0x0f==1)
							printf("Linefeed : Forward active\n");
						else if(value&0x0f==2)
							printf("Linefeed : Forward on-hook transmissio\n");
						else if(value&0x0f==3)
							printf("Linefeed : TIP open\n");
						else if(value&0x0f==4)
							printf("Linefeed : Ringing\n");
						else if(value&0x0f==5)
							printf("Linefeed : Reverse active\n");
						else if(value&0x0f==6)
							printf("Linefeed : Reverse on-hook transmission\n");
						else if(value&0x0f==7)
							printf("Linefeed : RING open\n");
						else
								printf("Linefeed : out of range!!\n");
					
					break;	
				case '9':
					printf(" 9. Set Linefeed states\n");
					value=0;
					printf("Linefeed = ");
  			  		scanf("%d",&value);
					ioctl( fd, SSP_SET_LINEFEED, &value);
					break;
				case 'a':
					printf(" a. Test phone transation\n");
					ioctl( fd, SSP_SLIC_DMA_TEST, NULL);
					break;
				case 'b':
					printf(" b. Test phone DTMF\n");
					ioctl( fd, SSP_SLIC_DTMFACTION_TEST, NULL);
					break;
				case 'Q':
					printf(" Q. Exit\n");
					inprog = 0;
					ioctl( fd, SSP_DISABLE_DIALTONE, NULL);
					ioctl( fd, SSP_PHONE_RING_STOP, NULL);
					break;
				default:
					printf("Out of Range !!\n");
				
			}//switch...
	  		
      	}//while...

      	close(fd);      

        printf("MESSAGE: Exit SLIC test program!!\n\n");
          usleep(10000); 
    return 0;
}
