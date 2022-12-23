/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <fcntl.h> //文件控制, 定义文件及其描述符的操作控制常数符号
#include <sys/stat.h> //文件状态

extern int sys_close(int fd);  //关闭文件系统调用
//复制文件句柄(文件描述符); fd欲复制的文件句柄; arg 指定新文件句柄的最小数值. 返回新文件句柄或出错码
static int dupfd(unsigned int fd, unsigned int arg)
{
//检查函数参数的有效性. 如果文件句柄值>NR_OPEN或句柄的文件结构不存在,则返回出错码并退出.
//如果指定的新句柄值arg>NR_OPEN,返回出错码; 实际上文件句柄就是进程文件结构指针数组索引号
	if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
//在当前进程的文件结构指针数组中寻找索引号>=arg, 但还没有使用的项.若找到的新句柄值arg >NR_OPEN
//(即没有空闲项)则返回出错码
	while (arg < NR_OPEN)
		if (current->filp[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
//对找到的空闲项(句柄)在执行时关闭标志位图close_on_exec中复位该句柄位.即在运行exec()类
//函数时,不会关闭用dup()创建的句柄,令该文件结构指针=原句柄fd的指针,且将文件引用计数+1,
//最后返回新的文件句柄arg
	current->close_on_exec &= ~(1<<arg);
	(current->filp[arg] = current->filp[fd])->f_count++;
	return arg;
}
//复制文件句柄系统调用; 复制指定文件句柄oldfd, 新文件句柄值=newfd, 如果newfd已打开,则首先
//关闭; oldfd 原文件句柄; newfd 新文件句柄
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);//若句柄newfd已经打开,则首先关闭
	return dupfd(oldfd,newfd);  //复制并返回新句柄
}
//复制文件句柄系统调用; 复制指定文件句柄oldfd, 新句柄的值=当前最小的未使用句柄值
//fildes 被复制的文件句柄. 返回新文件句柄值
int sys_dup(unsigned int fildes)
{
	return dupfd(fildes,0);
}
//文件控制系统调用函数; fd文件句柄; cmd控制命令; arg针对不同的命令有不同的含义.
//对于复制句柄命令F_DUPFD,arg是新文件句柄可取的最小值; 对于设置文件操作和访问标志命令F_SETFL
//arg是新的文件操作和访问模式. 对于文件上锁命令F_GETLK, F_SETLK,F_SETLKW, arg是指向flock
//结构的指针, linux0.11中没有实现文件上锁功能;
//若出错则所有操作返回-1, 成功F_DUPFD返回新文件句柄; F_GETFD返回文件句柄的当前执行时关闭
//标志close_on_exec; F_GETFL返回文件操作和访问标志
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
//检查给出的文件句柄的有效性; 根据不同命令cmd进行分别处理. 如果文件句柄值 >NR_OPEN或该
//句柄的文件指针=NULL,则返回出错码
	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD: //复制文件句柄
			return dupfd(fd,arg);
		case F_GETFD:  //取文件句柄的执行时关闭标志
			return (current->close_on_exec>>fd)&1;
		case F_SETFD: //设置执行时关闭标志; arg位0置位时设置,否则关闭
			if (arg&1)
				current->close_on_exec |= (1<<fd);
			else
				current->close_on_exec &= ~(1<<fd);
			return 0;
		case F_GETFL: //取文件状态标志和访问模式
			return filp->f_flags;
		case F_SETFL: //设置文件状态和访问模式(根据arg设置添加、非阻塞标志)
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW: //未实现
			return -1;
		default:
			return -1;
	}
}
