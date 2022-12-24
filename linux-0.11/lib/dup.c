/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
//复制文件描述符符函数; 该宏对应的函数是: int dup(int fd); 直接调用了系统中断int 0x80, 参数是: __NR_dup, fd是文件描述符
_syscall1(int,dup,int,fd)
