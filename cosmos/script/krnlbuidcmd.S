###################################################################
#		krnlbuidcmd自动化编译配置文件 Makefile		  #
#				彭东  
###################################################################

#include "config.h"
#ifdef CFG_X86_PLATFORM
ASM 	= nasm
CC		= gcc
LD		= ld
DD		= dd
RM		= rm
OYFLAGS		= -O binary

# CFLAGS 去掉 -O2 ,增加 -g
CFLAGS      = $(HEADFILE_PATH) -c -O0 -g -m64 -mtune=generic -mcmodel=large -mno-red-zone -std=c99 -fexec-charset=UTF-8 -Wall -Wshadow  -W -Wconversion -Wno-sign-conversion -fno-stack-protector -fno-zero-initialized-in-bss -fomit-frame-pointer -fno-builtin -fno-common  -fno-ident -ffreestanding  -Wno-unused-parameter -Wunused-variable #-save-temps -fdata-sections -gstabs+

# LDFLAGS 去掉 -s
LDFLAGS		= -static -T cosmoslink.lds -n -Map cosmos.map

CPPFLGSLDS	= $(HEADFILE_PATH) -E -P 
ASMFLGS		= $(HEADFILE_PATH) -f elf64
OBJCOPY = objcopy

# OJCYFLAGS 去掉 -S
OJCYFLAGS = -O binary
#endif

