/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
//数据块写函数, 向指定设备从给定偏移处写入指定长度数据; dev设备号; pso设备文件中偏移量指针; buf:用户空间中缓冲区地址; count要传送的字节数
//返回已写入字节数,若没有写入任何字节或出错,则返回出错码; 对于内核来说,写操作是向高速缓冲区中写入数据, 什么时候数据最终写入设备是由高速
//缓冲管理程序决定并处理的,另外因为块设备是以块为单位进行读写的,因此对于写开始位置不处于块起始处时,需要先将开始字节所在的整个块读出,然后
//将需要写的数据从写开始处填写满该块,再将完整的一块数据写盘(即交由高速缓冲程序去处理)
int block_write(int dev, long * pos, char * buf, int count)
{
//首先由文件中位置pos换算成开始读写盘块的块序号block, 并求出需写第1字节在该块中的偏移位置offset
	int block = *pos >> BLOCK_SIZE_BITS;  // pos所在文件数据块号
	int offset = *pos & (BLOCK_SIZE-1); //pos在数据块中偏移值
	int chars;
	int written = 0;
	struct buffer_head * bh;
	register char * p;  //局部寄存器变量,被存放在寄存器中
//然后针对要写入的字节数count, 循环执行以下操作: 直到数据全部写入,在循环执行过程中先计算在当前处理的数据块中可写入的字节数.如果需要写入
//的字节数填不满一块,那么就只需要写count字节. 如果正好要写1块数据内容, 则直接申请1块高速缓冲块,并把用户数据放入即可. 否则就需要读入
//将被写入部分数据的数据块,并预读下两块数据,然后将块号递增1, 为下次操作做好准备,如果缓冲块操作失败,则返回已写字节数,如果没有写入任何字节
//则返回出错码(负数)
	while (count>0) {
		chars = BLOCK_SIZE - offset;  // 本块可写入的字节数
		if (chars > count)
			chars=count;
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
			bh = breada(dev,block,block+1,block+2,-1);
		block++;
		if (!bh)
			return written?written:-EIO;
//接着把指针p指向读出数据的缓冲块中开始写入数据的位置处. 若最后一次循环写入的数据不足一块,则需从块开始处填写(修改)所需的字节.因此这里需要
//预先设置offset为0. 此后将文件中偏移指针pos前移此次将要写的字节数chars, 并累计到这些要写的字节数到统计值written中,再把还需要写的计数
//值count减去此次要写的字节数chars, 然后从用户缓冲区复制chars个字节到p指向的高速缓冲区中开始写入的位置处,复制完后就设置该缓冲区块
//已修改标志,并释放该缓冲区(即该缓冲区引用计数递减1)
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		written += chars;  //累计写入字节数
		count -= chars;
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		bh->b_dirt = 1;
		brelse(bh);
	}
	return written; //返回已写入的字节数,正常退出
}
//数据块读函数, 从指定设备和位置处读入指定长度数据到用户缓冲区中. dev设备号; pos设备文件中偏移量指针; buf 用户空间中缓冲取地址
//count 要传送的字节数; 返回以读入字节数; 若没有读入任何字节或出错,则返回出错码
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
//由文件中位置pos换算成开始读写盘块的块序号block, 并求出需读第1字节在块中的偏移位置offset
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int read = 0;
	struct buffer_head * bh;
	register char * p; //局部寄存器变量, 被存放在寄存器中
//然后针对要读入的字节数count, 循环执行以下操作, 直到数据全部读入. 在循环执行过程中,先计算在当前处理的数据块中需读入的字节数,如果需要
//读入的字节数还不满一块,就只需读count字节,然后调用读块函数breada()读入需要的数据块,并预读下两块数据,如果读操作出错,则返回已读字节数
//如果没有读入任何字节,则返回出错码. 然后将块号递增1, 为下次操作做准备,如果缓冲块操作失败,则返回已写字节数, 如果没有读入任何字节,
//返回出错码(负数)
	while (count>0) {
		chars = BLOCK_SIZE-offset;
		if (chars > count)
			chars = count;
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		block++;
//把指针p指向读出盘块的缓冲块中开始读入数据的位置处, 若最后一次循环读操作的数据不足一块,则需要从块起始读取所需字节,因此这里需要预先设置
//offset=0, 此后将文件中偏移指针pos前移此次要读取的字节数chars, 且累加到这些要读的字节数到统计值read中, 在把还需要读的计数值count
//减去此次要读的字节数chars, 然后从高速缓冲块中p指向的开始读的位置处复制chars个字节到用户缓冲区中, 同时把用户缓冲区指针前移
//本次复制完后就释放该缓冲块
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;  //累计读入字节数
		count -= chars;
		while (chars-->0)
			put_fs_byte(*(p++),buf++);
		brelse(bh);
	}
	return read; //返回已读取的字节数, 正常退出
}
