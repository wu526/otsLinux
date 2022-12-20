/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <termios.h>//终端输入输出函数头文件,主要定义控制异步通信口的终端接口

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
//波特率因子数组(也叫除数数组).
static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};
//修改传输波特率; tty终端对应的tty数据结构; 在除数锁存标志DLAB置位情况下,通过端口0x3f8,0x3f9
//向UART分别写入波特率因子低字节和高字节, 写完后再复位DLAB位. 对于串口2端口是0x2f8,0x2f9
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;
//先检查参数tty指定的终端是否是串行终端,若不是则退出.对于串口终端的tty结构,其读缓冲队列data
//字段存放着串行端口基址0x3f8,0x2f8, 而一般控制台终端的tty结构的read_q.data字段值=0. 然后
//从终端termios结构的控制模式标志集中取得已设置的波特率索引号,并据此从波特率因子数组中取得
//对应的波特率因子值quot. CBAUD是控制模式标志集中波特率位屏蔽码
	if (!(port = tty->read_q.data))
		return;
	quot = quotient[tty->termios.c_cflag & CBAUD];
//把波特率因子quot写入串行端口对应UART芯片的波特率因子锁存器中. 在写之前要先把线路控制寄存器
//LCR的除数锁存访问bit位DLAB(位7)置1, 然后把16位的波特率因子低高字节分别写入端口0x3f8,0x3f9
//(分别对应波特率因子低、高字节锁存器). 最后复位LCR的DLAB标志位.
	cli();
	outb_p(0x80,port+3);		/* set DLAB */ //设置除数锁定标志DLAB
	outb_p(quot & 0xff,port);	/* LS of divisor */  // 输出因子低字节
	outb_p(quot >> 8,port+1);	/* MS of divisor */ //输出因子高字节
	outb(0x03,port+3);		/* reset DLAB */ //复位DLAB
	sti();
}
//刷新tty缓冲队列; queue指定的缓冲队列指针. 令缓冲队列的头指针等于尾指针,从而达到清空缓冲区的目的
static void flush(struct tty_queue * queue)
{
	cli();
	queue->head = queue->tail;
	sti();
}
//等待字符发送出去
static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}
//发送BREAK控制字符
static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}
//取终端termios结构信息 tty 指定终端的tty结构指针; termios存放termios结构的用户缓冲区
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;
//验证用户缓冲区指针所指内存区容量是否足够; 不够则分配内存,然后复制指定终端的termios结构
//信息到用户缓冲区中,最后返回0
	verify_area(termios, sizeof (*termios));
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );
	return 0;
}
//设置终端termios结构信息. tty指定终端的tty结构指针, termios用户数据区termios指针
static int set_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;
//把用户数据区中termios结构信息复制到指定终端tty结构的termios结构中,因为用户可能已修改了
//终端串行口传输波特率,所以这里再根据termios结构中的控制模式标志c_flag 中的波特率信息修改
//串行UART芯片内的传输波特率,最后返回0
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);
	change_speed(tty);
	return 0;
}
//读取termio结构中的信息; tty指定终端的tty结构指针; termio 保存termio 结构信息的用户缓冲区
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;
//验证用户的缓冲区指针所指内存容量是否足够. 如果不够则分配内存. 然后将termio结构的信息复制
//到林斯和termio中, 这两个结构基本相同,但输入、输出、控制和本地标志集数据类型不同, 前者的是
//long,后者的是short, 因此先复制到临时termio结构中目的是为了进行数据类型转换.
	verify_area(termio, sizeof (*termio));
	tmp_termio.c_iflag = tty->termios.c_iflag;
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
//逐字节地把临时termio结构中的信息复制到用户termio结构缓冲区中.并返回0		
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio );
	return 0;
}

/*
 * This only works as the 386 is low-byt-first //适用于低字节在前的 386CPU
 */
//设置终端termio结构信息 tty-指定终端的tty结构指针; termio 用户数据区中termio结构
//将用户缓冲区termio的信息复制到终端的termio 结构中,返回0
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;
//先复制用户数据区中termio 结构信息到临时termio结构中.然后再将termio结构的信息复制到tty
//的termio中. 目的是为了对其中模式标志集的类型进行转换, 即从termio的短整数类型转换成termio
//的长整数类型, 但两种结构的c_line,c_cc[]字段是完全相同的.
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i=0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
//最后因为用户有可能已修改了终端串行口传输波特率,所以这里再根据termio结构中的控制模式标志
//c_cflag 中的波特率信息修改串行UART芯片内的传输波特率,返回0
	change_speed(tty);
	return 0;
}
//tty终端设备输入输出控制函数, dev设备号, cmd:iotcl命令, arg操作参数指针.
//首先根据参数给出的设备号找到对应终端tty结构,然后根据控制命令cmd分别进行处理
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
//首先根据设备号取得tty子设备号,从而取得终端的tty结构. 若主设备号是5(控制终端),则进程的tty
//字段即是tty子设备号. 此时如果进程的tty子设备号是负数,表明该进程没有控制终端,即不能发出该ioctl
//调用,于是显示出错信息并停机. 如果主设备号不是5而是4, 就可以从设备号中取出子设备号,子设备号
//可以是0(控制台终端),1(串口1终端),2(串口2终端).
	if (MAJOR(dev) == 5) {
		dev=current->tty;
		if (dev<0)
			panic("tty_ioctl: dev<0");
	} else
		dev=MINOR(dev);
//根据子设备号和tty表,可以取得对应终端的tty结构,于是让tty指向对应子设备号的tty结构.然后在
//根据参数提供的ioctl命令cmd 进行分别处理
	tty = dev + tty_table;
	switch (cmd) {
		case TCGETS: //取相应终端termio结构信息,此时参数arg是用户缓冲区指针
			return get_termios(tty,(struct termios *) arg);
		case TCSETSF: //设置termio之前,需要先等待输出队列中所有数据处理完毕,
			flush(&tty->read_q); /* fallthrough */ //且刷新输入队列,再接着指向下面设置终端termio的操作
		case TCSETSW://设置终端termio的信息前,需要先等待输出队列中所有数据处理完.对于修改参数
			wait_until_sent(tty); /* fallthrough */ //会影响输出的情况,就需要使用这种方式
		case TCSETS://设置相应终端termio信息,此时参数arg保存termio结构的用户缓冲区指针
			return set_termios(tty,(struct termios *) arg);
		case TCGETA://取相应终端termio的信息, 此时参数arg是用户缓冲区指针
			return get_termio(tty,(struct termio *) arg);
		case TCSETAF: //设置termio结构信息前,需要先等待输出队列中所有数据处理完毕,且刷新输入队列.
			flush(&tty->read_q); /* fallthrough */ //再接着执行下面设置终端termio的操作
		case TCSETAW://设置termio信息前,需要先等待输出队列中所有数据处理完,对于修改参数会影响
			wait_until_sent(tty); /* fallthrough *///输出的情况,就需要使用这种形式
		case TCSETA://设置相应终端termio结构,此时参数arg是保存termio的用户缓冲区指针
			return set_termio(tty,(struct termio *) arg);
		case TCSBRK://如果参数arg值=0,则等待输出队列处理完毕,闭关发送一个break
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		case TCXONC://开始/停止流控制,如果参数值=0,则挂起输出;如果=1,则恢复挂起的输出;
//=2则挂起输入; =3则重新开启挂起的输入
			return -EINVAL; /* not implemented */
		case TCFLSH: //刷新已写输出但还没有发送或已收但还没有读的数据.如果arg=0, 则刷新
//输入队列; =1则刷新输出队列; =2则刷新输入和输出队列		
			if (arg==0)
				flush(&tty->read_q);
			else if (arg==1)
				flush(&tty->write_q);
			else if (arg==2) {
				flush(&tty->read_q);
				flush(&tty->write_q);
			} else
				return -EINVAL;
			return 0;
		case TIOCEXCL://设置终端串行线路专用模式
			return -EINVAL; /* not implemented */
		case TIOCNXCL: //复位终端串行线路专用模式
			return -EINVAL; /* not implemented */
		case TIOCSCTTY:  // 设置tty为控制终端,TIOCNOTTY 禁止为控制终端
			return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP://读取终端设备进程组号,首先验证用户缓冲区长度,然后复制tty的pgrp字段
//到用户缓冲区,arg是用户缓冲区指针		
			verify_area((void *) arg,4);
			put_fs_long(tty->pgrp,(unsigned long *) arg);
			return 0;
		case TIOCSPGRP://设置终端设备的进程组号pgrp, arg是用户缓冲区中pgrp的指针
			tty->pgrp=get_fs_long((unsigned long *) arg);
			return 0;
		case TIOCOUTQ: //返回输出队列中还未送出的字符数.首先验证用户缓冲区长度,然后复制
//队列中字符数给用, arg是用户缓冲区指针.		
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
			return 0;
		case TIOCINQ: //返回输入队列中还未读取的字符数,首先证明用户缓冲区长度,然后复制队列
//中字符数给用户, arg是用户缓冲区指针		
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		case TIOCSTI://模拟终端输入操作. 该命令以一个指向字符的指针作为参数,并假设该字符
//是在终端上键入的.用户必须在该控制终端上具有超级用户权限或具有读许可权限		
			return -EINVAL; /* not implemented */
		case TIOCGWINSZ:  // 读取终端设备窗口大小信息
			return -EINVAL; /* not implemented */
		case TIOCSWINSZ: // 设置终端设备窗口大小信息
			return -EINVAL; /* not implemented */
		case TIOCMGET: //返回MODEM状态控制引线的当前状态bit位标志集
			return -EINVAL; /* not implemented */
		case TIOCMBIS: // 设置单个modem状态控制引线的状态
			return -EINVAL; /* not implemented */
		case TIOCMBIC: //复位带个MODEM状态控制引线状态
			return -EINVAL; /* not implemented */
		case TIOCMSET:  // 设置MODEM状态引线的状态. 如果某bit位置位,则modem对应的状态引线将置为有效
			return -EINVAL; /* not implemented */
		case TIOCGSOFTCAR:  // 读取软件载波检测标志 1开启,0关闭
			return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:  // 设置软件载波检测标志 1开启,0关闭
			return -EINVAL; /* not implemented */
		default:
			return -EINVAL;
	}
}
