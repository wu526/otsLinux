/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <linux/sched.h>

extern int tty_ioctl(int dev, int cmd, int arg);
//定义输入输出控制(ioctl)函数指针类型
typedef int (*ioctl_ptr)(int dev,int cmd,int arg);
//取系统中设备钟数的宏
#define NRDEVS ((sizeof (ioctl_table))/(sizeof (ioctl_ptr)))
//ioctl操作函数指针表
static ioctl_ptr ioctl_table[]={
	NULL,		/* nodev */
	NULL,		/* /dev/mem */
	NULL,		/* /dev/fd */
	NULL,		/* /dev/hd */
	tty_ioctl,	/* /dev/ttyx */
	tty_ioctl,	/* /dev/tty */
	NULL,		/* /dev/lp */
	NULL};		/* named pipes */
//系统调用函数-输入输出控制函数; 该函数首先判断参数给出的文件描述符是否有效.然后根据对应i
//节点钟文件属性判断文件类型,并根据具体文件类型调用相关的处理函数. fd 文件描述符; cmd:命令码
//arg 参数; 成功返回0,否则返回出错码
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int dev,mode;
//判断给出的文件描述符的有效性; 如果文件描述符超出可打开的文件数,或者对应描述符的文件结构指针
//为空,则返回出错码.
	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
//取对应文件的属性,并据此判断文件的类型. 如果该文件既不是字符设备文件也不是块设备文件,则返回
//出错码; 若是字符或块设备文件,则从文件的i节点中取设备号,如果设备号>系统现有的设备数,返回出错码		
	mode=filp->f_inode->i_mode;
	if (!S_ISCHR(mode) && !S_ISBLK(mode))
		return -EINVAL;
	dev = filp->f_inode->i_zone[0];
	if (MAJOR(dev) >= NRDEVS)
		return -ENODEV;
//根据io控制表ioctl_table查得对应设备的ioctl函数指针,并调用该函数. 如果该设备在ioctl函数
//指针表中没有对应函数,则返回出错码		
	if (!ioctl_table[MAJOR(dev)])
		return -ENOTTY;
	return ioctl_table[MAJOR(dev)](dev,cmd,arg);
}
