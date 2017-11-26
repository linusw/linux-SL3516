#
# Makefile for misc devices that really don't fit anywhere else.
#

ifeq ($(CONFIG_BLK_DEV_LOOP_AES),y)
AES_X86_ASM=n
ifeq ($(CONFIG_X86),y)
ifneq ($(CONFIG_X86_64),y)
  AES_X86_ASM=y
endif
endif
ifeq ($(AES_X86_ASM),y)
  obj-y       += aes-x86.o md5-x86.o crypto-ksym.o
  AFLAGS_aes-x86.o := -DUSE_UNDERLINE=1
else
ifeq ($(CONFIG_X86_64),y)
  obj-y       += aes-amd64.o md5-amd64.o crypto-ksym.o
  AFLAGS_aes-amd64.o := -DUSE_UNDERLINE=1
else
  obj-y       += aes.o md5.o crypto-ksym.o
  CFLAGS_aes.o := -DDATA_ALWAYS_ALIGNED=1
endif
endif
endif

obj- := misc.o	# Dummy rule to force built-in.o to be made

obj-$(CONFIG_IBM_ASM)	+= ibmasm/
obj-$(CONFIG_HDPU_FEATURES)	+= hdpuftrs/
