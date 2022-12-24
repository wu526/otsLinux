/*
 *  linux/lib/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <stdarg.h>
//打开文件函数, 打开并有可能创建一个文件; filename 文件名; flag 文件打开标志; 返回文件描述符,若出错则置出错码, 并返回-1
int open(const char * filename, int flag, ...)
{
	register int res;
	va_list arg;
//利用va_start()宏函数,取得flag后面参数的指针,然后调用系统中断 int 0x80, 功能进行文件打开操作; %0 eax(res, 返回的描述符或出错码)
//%1 eax(系统调用功能号 __NR_open); %2 ebx(filename 文件名); %3 ecx(flag 打开文件标志flag); %4 edx(文件属性mode)
	va_start(arg,flag);
	__asm__("int $0x80"
		:"=a" (res)
		:"0" (__NR_open),"b" (filename),"c" (flag),
		"d" (va_arg(arg,int)));
	if (res>=0) //系统中断调用返回值>=0表示是一个文件描述符,则直接返回
		return res;
	errno = -res; //否则说明返回值小于0, 则代表一个出错码, 设置该出错码并返回-1
	return -1;
}
