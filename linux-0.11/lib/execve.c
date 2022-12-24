/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
//加载并执行子进程(其他程序)函数; 该宏对应的函数声明是 int execve(const char *file, char **argv, char **envp);
//file被执行程序文件名; argv命令行参数指针数组; envp 环境变量指针数组; 直接调用了系统中断 int 0x80, 参数是__NR_execve
_syscall3(int,execve,const char *,file,char **,argv,char **,envp)
