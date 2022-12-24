#ifndef _TERMIOS_H
#define _TERMIOS_H

#define TTY_BUF_SIZE 1024 //tty中的缓冲区长度
//0x54是一个魔数,目的是为了使这些常数唯一
/* 0x54 is just a magic number to make these relatively uniqe ('T') */
//tty设备的ioctl调用命令集, ioctl将命令编码在低位字中; 下面名称TC[*]的含义是tty控制命令;
#define TCGETS		0x5401 //取相应终端termios结构中的信息, 参见tcgetattr()
#define TCSETS		0x5402 //设置相应终端termios结构中的信息, 参见 tcsetattr(), TCSANOW
//在设置终端termios的信息前,需要先等待输出队列中所有数据处理完(耗尽).对于修改参数会影响输出的情况,
//需要使用这种形式,参见tcseattr() TCSADRAIN
#define TCSETSW		0x5403 
//在设置termios的信息前,需要先等待输入队列中的所有数据处理完,且刷新(清空)输入队列. 再设置(参见 tcsetattr(), TCSAFLUSH)
#define TCSETSF		0x5404
#define TCGETA		0x5405 //取相应终端termio结构中的信息, 参见 tcgetattr()
#define TCSETA		0x5406 //设置相应终端termio中的信息, 参见 tcsetattr(), TCSANOW选项
//在设置终端termio的信息前,需要先等待输出队列中数据处理完(耗尽), 对于修改参数会影响输出的情况,就需要使用这种形式(见tcsetattr(),TCSADRAIN)
#define TCSETAW		0x5407
//在设置termio信息前,需要先等待输出队列中所有数据处理完,且刷新(清空)输入队列,再设置(参见 tcsetattr(), TCSAFLUSH)
#define TCSETAF		0x5408
#define TCSBRK		0x5409 //等待输出队列处理完毕(空), 如果参数值=0,则发送一个break(参考tcsendbreak(), tcdrain())
//开始/停止控制,如果参数值=0, 则挂起输出; 如果是1,则重新开启挂起的输出; 如果是2,则挂起输入; 如果是3,则重新开启挂起的输入(参见tcflow())
#define TCXONC		0x540A
//刷新已写输出但还没发送或已收但还没有读数据. 如果参数是0,则刷新(清空)输入队列; 如果是1,则刷新输出队列; 如果是2,则刷新输入和输出队列(参见tcflush())
#define TCFLSH		0x540B
//下面名称TIOC[*]的含义是tty输入输出控制命令;
#define TIOCEXCL	0x540C //设置终端串行线路专用模式
#define TIOCNXCL	0x540D //复位终端串行线路专用模式
#define TIOCSCTTY	0x540E //设置tty为控制终端(TIOCNOTTY-禁止tty为控制终端)
#define TIOCGPGRP	0x540F // 读取指定终端设备进程的组id(参见tcgetpgrp())
#define TIOCSPGRP	0x5410 //设置指定终端设备进程组id(参见tcsetpgrp())
#define TIOCOUTQ	0x5411 //返回输出队列中还未送出的字符数
//模拟终端输入,该命令以一个指向字符的指针作为参数,并假装该字符是在终端上键入的.用户必须在该控制终端上具有超级用户权限或具有读许可权限
#define TIOCSTI		0x5412
#define TIOCGWINSZ	0x5413 //读取终端设备窗口大小信息 参见 winsize 结构
#define TIOCSWINSZ	0x5414 //设置终端设备窗口大小信息 参见 winsize 结构
#define TIOCMGET	0x5415 //返回modem状态控制引线的当前状态比特位标志集
#define TIOCMBIS	0x5416 //设置单个modem状态控制引线的状态(true,false)(Individual control line set)
#define TIOCMBIC	0x5417 //复位单个modem状态控制引线的状态(Individual control line clear)
#define TIOCMSET	0x5418 //设置modem状态引线的状态, 如果某一比特位置位,则modem对应的状态引线将置为有效
//读取软件载波检测标志(1开启,0关闭); 对于本地连接的终端或其他设备,软件载波标志是开启的,对于使用modem线路的终端或设备则是关闭的.为了
//能使用这两个ioctl调用,tty线路应该是以O_NDELAY方式打开,这样open()就不会等待载波
#define TIOCGSOFTCAR	0x5419
#define TIOCSSOFTCAR	0x541A //设置软件载波检测标志(1开启,0关闭)
#define TIOCINQ		0x541B //返回输入队列中还未取走字符的数目
//窗口大小(window size)属性介机构, 在窗口环境中可用于基于屏幕的应用程序. ioctls中的TIOCGWINSZ和TIOCSWINSZ可用来读取或设置这些信息
struct winsize {
	unsigned short ws_row; //窗口字符行数
	unsigned short ws_col; //窗口字符列数
	unsigned short ws_xpixel; //窗口宽度, 像素值
	unsigned short ws_ypixel; //窗口高度, 像素值
};
//AT&T系统V的termio结构
#define NCC 8 //termio结构中控制字符数组的长度
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */ //线路规程(速率)
	unsigned char c_cc[NCC];	/* control characters */ //控制字符数组
};
//POSIX的termios结构
#define NCCS 17 //termios结构中控制字符数组长度
struct termios {
	unsigned long c_iflag;		/* input mode flags */
	unsigned long c_oflag;		/* output mode flags */
	unsigned long c_cflag;		/* control mode flags */
	unsigned long c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */ //线路规程(速率)
	unsigned char c_cc[NCCS];	/* control characters */ //控制字符数组
};

/* c_cc characters */ //c_cc数组中的字符, 参考 c_cc01.png
//以下是控制字符数组c_cc[]中项的索引值. 该数组初始值定义在 include/linux/tty.h中. 程序可以更改这个数组中的值,如果定义了
//_POSIX_VDISABLE(\0),那么当数组某一项值= _POSIX_VDISABLE的值时,表示禁止使用数组中相应的特殊字符
#define VINTR 0 //c_cc[VINTR] = INTR (^C), \003, 中断字符
#define VQUIT 1 //c_cc[VQUIT] = QUIT, (^\), \034, 退出字符
#define VERASE 2 //c_cc[VERASE] = ERASE (^H), \177 擦除字符
#define VKILL 3 //c_cc[VKILL] = KILL, ^U \025, 终止字符(删除行)
#define VEOF 4 //c_cc[VEOF]=EOF, ^D \004 文件结束字符
#define VTIME 5 
#define VMIN 6
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16

/* c_iflag bits */ //参考c_iflag01.png
#define IGNBRK	0000001
#define BRKINT	0000002
#define IGNPAR	0000004
#define PARMRK	0000010
#define INPCK	0000020
#define ISTRIP	0000040
#define INLCR	0000100
#define IGNCR	0000200
#define ICRNL	0000400
#define IUCLC	0001000
#define IXON	0002000
#define IXANY	0004000
#define IXOFF	0010000
#define IMAXBEL	0020000

/* c_oflag bits */ //参考c_oflag01.png
#define OPOST	0000001
#define OLCUC	0000002
#define ONLCR	0000004
#define OCRNL	0000010
#define ONOCR	0000020
#define ONLRET	0000040
#define OFILL	0000100
#define OFDEL	0000200
#define NLDLY	0000400
#define   NL0	0000000
#define   NL1	0000400
#define CRDLY	0003000
#define   CR0	0000000
#define   CR1	0001000
#define   CR2	0002000
#define   CR3	0003000
#define TABDLY	0014000
#define   TAB0	0000000
#define   TAB1	0004000
#define   TAB2	0010000
#define   TAB3	0014000
#define   XTABS	0014000
#define BSDLY	0020000
#define   BS0	0000000
#define   BS1	0020000
#define VTDLY	0040000
#define   VT0	0000000
#define   VT1	0040000
#define FFDLY	0040000
#define   FF0	0000000
#define   FF1	0040000

/* c_cflag bit meaning */ //参考c_cflag01.png
#define CBAUD	0000017
#define  B0	0000000		/* hang up */
#define  B50	0000001
#define  B75	0000002
#define  B110	0000003
#define  B134	0000004
#define  B150	0000005
#define  B200	0000006
#define  B300	0000007
#define  B600	0000010
#define  B1200	0000011
#define  B1800	0000012
#define  B2400	0000013
#define  B4800	0000014
#define  B9600	0000015
#define  B19200	0000016
#define  B38400	0000017
#define EXTA B19200
#define EXTB B38400
#define CSIZE	0000060
#define   CS5	0000000
#define   CS6	0000020
#define   CS7	0000040
#define   CS8	0000060
#define CSTOPB	0000100
#define CREAD	0000200
#define CPARENB	0000400
#define CPARODD	0001000
#define HUPCL	0002000
#define CLOCAL	0004000
#define CIBAUD	03600000		/* input baud rate (not used) */
#define CRTSCTS	020000000000		/* flow control */

#define PARENB CPARENB
#define PARODD CPARODD

/* c_lflag bits */ //c_lflag01.png
#define ISIG	0000001
#define ICANON	0000002
#define XCASE	0000004
#define ECHO	0000010
#define ECHOE	0000020
#define ECHOK	0000040
#define ECHONL	0000100
#define NOFLSH	0000200
#define TOSTOP	0000400
#define ECHOCTL	0001000
#define ECHOPRT	0002000
#define ECHOKE	0004000
#define FLUSHO	0010000
#define PENDIN	0040000
#define IEXTEN	0100000

/* modem lines */ //modemlines01.png
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */ //tcflow(), TCXONC使用的符号常数
#define	TCOOFF		0 //挂起输出
#define	TCOON		1 //重启被挂起的输出
#define	TCIOFF		2 //系统传输一个STOP字符,使设备停止向系统传输数据
#define	TCION		3 //系统传输一个START字符,使设备开始向系统传输数据

/* tcflush() and TCFLSH use these */ //tcflush()和TCFLSH使用这些符号常数
#define	TCIFLUSH	0 //清接收到的数据但不读
#define	TCOFLUSH	1 //清已写的数据但不传输
#define	TCIOFLUSH	2 //清收到的数据但不读,清已写的数据但不传输

/* tcsetattr uses these */ //tcsetattr()使用这些符号常数
#define	TCSANOW		0 //改变立即发生
#define	TCSADRAIN	1 //改变在所有已写的输出被传输后发生
#define	TCSAFLUSH	2 //改变在所有已写的输出被传输后且在所有接收到但还没有读取的数据被丢弃后发生.

typedef int speed_t; //波特率数值类型
//以下函数注释参见 tc_functions01.png
extern speed_t cfgetispeed(struct termios *termios_p);
extern speed_t cfgetospeed(struct termios *termios_p);
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
extern int tcdrain(int fildes);
extern int tcflow(int fildes, int action);
extern int tcflush(int fildes, int queue_selector);
extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsendbreak(int fildes, int duration);
extern int tcsetattr(int fildes, int optional_actions,
	struct termios *termios_p);

#endif
