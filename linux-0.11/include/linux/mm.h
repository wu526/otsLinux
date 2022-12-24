#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096 // 定义一页内存页面字节数 高速缓冲块长度是1024字节

extern unsigned long get_free_page(void); //在主内存中取空闲物理页, 如果没有可用内存了,则返回0
extern unsigned long put_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr); //释放物理地址addr开始的1页内存

#endif
