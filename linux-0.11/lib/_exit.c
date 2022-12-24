/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__  //定义一个符号常量
#include <unistd.h> //Linux标准头文件, 定义了各种符号常数和类型, 并申明了各种函数, 如定义了__LIBARAY__则还含系统调用号和内嵌汇编等
//内核使用的程序(退出)终止函数; 直接调用系统中断 int 0x80, 功能号 __NR_exit; exit_code 退出码
volatile void _exit(int exit_code)
{//%0 eax(系统调用号, __NR_exit); %1 ebx(退出码 exit_code)
	__asm__("int $0x80"::"a" (__NR_exit),"b" (exit_code));
}
