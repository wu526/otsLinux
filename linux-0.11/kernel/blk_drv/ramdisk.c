/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91, 他提出并实现了ext2文件系统,并升级到ext3
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h> //文件系统头文件
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>  //内存拷贝头文件
//定义RAM盘主设备符号常数,在驱动程序中主设备号必须在包含blk.h文件前定义.
#define MAJOR_NR 1
#include "blk.h"

char	*rd_start;  // 虚拟盘在内存中的起始位置,该位置会在初始化函数 rd_init()中确定. rd是ramdisk的缩写
int	rd_length = 0;  // 虚拟盘所占内存大小(字节)
//虚拟盘当前请求项操作函数. 该函数的程序结构与硬盘的do_hd_request()函数类似,在低级块设备接口函数ll_rw_block()建立虚拟盘(rd)的请求项
//并添加到rd的链表中后,就调用该函数对rd当前请求进行处理. 该函数首先计算当前请求项中指定的起始扇区对应虚拟盘所处内存的起始位置addr和要求
//的扇区数所对应的字节长度值len,然后根据请求项中的命令进行操作.若是写命令write,就把请求项所指缓冲区的数据直接复制到内存位置addr处.
//读则相反,数据复制完成后即可直接调用 end_request()对本次请求项作结束处理.然后跳转到函数开始处在处理下一个请求项,若没有请求项则退出.
void do_rd_request(void)
{
	int	len;
	char	*addr;
//先检测请求项的合法性.若没有请求项在退出.然后计算请求项处理的虚拟盘中起始扇区在物理内存中对应的地址addr和占用的内存字节长度值len.
//然后获得请求项中的起始扇区对应的内存起始位置和内存长度.其中 sector<<9表示sector*512,换算成字节值.
	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
//如果当前请求项中子设备号!=1或者对应内存起始位置大于虚拟盘末尾,则结束请求项,并跳转到repeat处处理下一个虚拟盘请求项.	
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
//进行实际的读写操作,如果是写命令(write)则将请求项中缓冲区的内容复制到地址addr处,长度=len字节; 如果是读命令(read),则将addr开始
//的内存复制到请求项缓冲区中,长度=len字节.否则显示命令不存在,死机.
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
	end_request(1);// 在请求项成功处理后,置更新标志,并继续处理本设备的下一请求项.
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved. 返回内存虚拟盘 ramdisk 所需的内存量
 */
//虚拟盘初始化函数;该函数首先设置虚拟盘设备的请求项处理函数指针指向do_rd_request(),然后确定虚拟盘在物理内存中的起始地址、占用字节长度
//并对整个虚拟盘区清零,最后返回盘区长度. 当linux/makefile 文件中设置过RAMDISK不为0时,表示系统中会创建RAM虚拟盘设备.这种情况下的内核
//初始化过程中,本函数就会被调用,该函数的第二个参数length会被赋值为 RAMDISK * 1024 字节
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;  // do_rd_request()
	rd_start = (char *) mem_start;  // 对于16M系统该值为4M
	rd_length = length;  // 虚拟盘区域长度值
	cp = rd_start;
	for (i=0; i < length; i++)  //盘区清零
		*cp++ = '\0';
	return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 * 如果根文件系统设备(root device)是ramdisk,则尝试加载, root device原先是指向软盘的,将它改成指向ramdisk
 */
//尝试把根文件系统加载到虚拟盘中. 该函数将在内核设置函数setup()中被调用, block=256表示根文件系统映像文件被存储于boot盘第256磁盘块开始处
void rd_load(void)
{
	struct buffer_head *bh;  // 高速缓冲块头指针
	struct super_block	s;  // 文件超级块结构
	int		block = 256;	/* Start at block 256 */  // 开始于256盘块
	int		i = 1;
	int		nblocks;  // 文件系统盘块总数
	char		*cp;		/* Move pointer */
//首先检测虚拟盘的有效性和完整性.如果ramdisk长度=0,则退出.否则显示ramdisk的大小以及内存起始位置.如果此时根文件设备不是软盘,则也退出	
	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)
		return;
//读取根文件系统的基本参数,即读软盘块256+1,256,256+2; block+1是指磁盘上的超级块. breada()用于读取指定的数据块,并标出还需要读的块,
//然后返回含有数据块的缓冲区指针. 如果返回NULL,则表示数据块不可读.然后把缓冲区中的磁盘超级块(d_super_block磁盘超级块结构)赋值到s变量
//中,并释放缓冲区.接着开始对超级块的有效性进行判断.如果超级块中文件系统魔数不对,则说明加载的数据块不是minix文件系统,于是退出.
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	if (s.s_magic != SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */ // 磁盘中无ramdisk映像文件,退出去执行通常的软盘引导
		return;
//然后试图把整个根文件系统读入到内存虚拟盘中,对于一个文件系统来说,其超级块结构的s_nzones字段中保存着总逻辑块数(区段数). 一个逻辑块中
//含有的数据块数则由字段s_log_zone_size 指定,因此文件系统中的数据块总数nblocks=逻辑块数*(2^每区段块数的次方),即nblocks=s_nzones
//*2^s_log_zone_size, 如果遇到文件系统中数据块总数>内存虚拟盘所能容纳的块数的情况,则不能执行加载操作,只能显示出错信息并返回
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
//能容纳得下文件系统总数据块数,则显示加载数据块信息,让cp指向内存虚拟盘起始处,然后开始执行循环操作将磁盘上根文件系统映像文件加载到虚拟盘
//上.在操作过程中,如果一次需要加载的盘块数>2,就用超前预读函数breada,否则就使用bread()进行单位块读取.若在读盘过程中出现IO错误,就只能
//放弃和加载过程返回.所读取的磁盘块会使用memcpy函数从高速缓冲区中复制到内存虚拟盘相应位置处,同时显示已加载的块数.显示字符串中的8进制\010表示一个制表符
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);  // 读取块数>2,则采用超前预读.
		else
			bh = bread(ROOT_DEV, block);  // 单块读取
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE); // 复制到cp处
		brelse(bh);
		printk("\010\010\010\010\010%4dk",i);  // 打印加载块计数值
		cp += BLOCK_SIZE;  // 虚拟盘指针迁移
		block++;
		nblocks--;
		i++;
	}
//当boot盘从256盘块开始的整个根文件系统加载完毕后,显示done,并把当前根文件设备号修改为虚拟盘的设备号0x0101,最后返回
	printk("\010\010\010\010\010done \n");
	ROOT_DEV=0x0101;
}
