#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h> //类型 头文件, 定义了基本的系统数据类型

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */ //NOCTTY和NDELAY 现在还没有实现
#define O_ACCMODE	00003 //文件访问模式屏蔽码 line7
//打开文件open()和文件控制函数fcntl()使用的文件访问模式,同时只能使用三者之一
#define O_RDONLY	   00 //以只读方式打开文件
#define O_WRONLY	   01 //以只写方式打开文件
#define O_RDWR		   02 //以读写方式打开文件
//下面是文件创建和操作标志, 用于open(), 可与上面访问模式用'位或'的方式一起使用
#define O_CREAT		00100	/* not fcntl */ //如果文件不存在就创建, fcntl()不用
#define O_EXCL		00200	/* not fcntl */ //独占使用文件标志
#define O_NOCTTY	00400	/* not fcntl */ //不分配控制终端
#define O_TRUNC		01000	/* not fcntl */ //若文件已存在且是写操作,则长度截为0
#define O_APPEND	02000 //亿添加方式打开和操作文件
#define O_NONBLOCK	04000	/* not fcntl */ //非阻塞方式打开和操作文件
#define O_NDELAY	O_NONBLOCK //非阻塞方式打开和操作文件 line17

/* Defines for fcntl-commands. Note that currently
 * locking isn't supported, and other things aren't really
 * tested. //定义了fcntl的命令, 目前锁定还没有支持.其他命令实际上还没有测试过
 */
//文件句柄(描述符)操作函数fcntl()的命令(cmd)
#define F_DUPFD		0	/* dup */  //拷贝文件句柄为最小数值的句柄
#define F_GETFD		1	/* get f_flags */  //取句柄标志, 仅1个标志FD_CLOEXEC
#define F_SETFD		2	/* set f_flags */ //设置文件句柄标志
#define F_GETFL		3	/* more flags (cloexec) */ // 取文件状态标志和访问模式
#define F_SETFL		4 //设置文件状态标志和访问模式
//下面是文件锁定命令, fcntl()的第3个参数lock是指向flock结构的指针
#define F_GETLK		5	/* not implemented */ //返回阻止锁定的flock结构
#define F_SETLK		6 //设置(F_RDLCK或F_WRLCK)或清除(F_UNLCK)锁定
#define F_SETLKW	7 //等待设置或清除锁定

/* for F_[GET|SET]FL */ //用于F_GETFL或F_SETFL 在执行exec()簇函数时需要关闭的文件句柄(执行是关闭-close on execution)
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */ //实际上只要低位为1即可

/* Ok, these are locking features, and aren't implemented at any
 * level. POSIX wants them. //以下是锁定类型,任何函数中都还没有实现. POSIX标准要求这些类型
 */
#define F_RDLCK		0 //共享或读文件锁定
#define F_WRLCK		1 //独占或写文件锁定
#define F_UNLCK		2 //文件解锁

/* Once again - not implemented, but ... */
//文件锁定操作数据结构, 描述了受影响文件段的类型(l_type)、开始偏移(l_whence),想对偏移(l_start),锁定长度(l_len)和实施锁定的进程id
struct flock {
	short l_type; //锁定类型(F_RDLCK, F_WRLCK, F_UNLCK)
	short l_whence; //开始偏移(SEEK_SET, SEEK_CUR, SEEK_END)
	off_t l_start; //阻塞锁定的开始处, 相对偏移(字节数)
	off_t l_len;  //阻塞锁定的大小, 如果是0则为到文件末尾
	pid_t l_pid; //加锁的进程id
};
//创建文件或重写一个已存在文件. filename是要创建文件的文件名, mode是创建文件的属性
extern int creat(const char * filename,mode_t mode);
extern int fcntl(int fildes,int cmd, ...); // 文件句柄操作,会影响文件的打开. filedes 是文件句柄, cmd是操作命令, 该函数可有以下形式
//int fcntl(int fildes, int cmd); int fcntl(int fildes, int cmd, long arg);
//int fcntl(int fildes, int cmd, struct flock *lock);

//打开文件,在文件与文件句柄之间建立联系. filename是想打开文件的文件名, flags是上面line7~line17的标志组合
extern int open(const char * filename, int flags, ...); 

#endif
