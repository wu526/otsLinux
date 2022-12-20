/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl.
 */
#include <ctype.h> // 字符类型头文件,定义了一些有关字符类型判断和转换的宏
#include <errno.h>  // 错误号头文件
#include <signal.h>  // 信号头文件
//一些信号在信号位图中对应的bit屏蔽位
#define ALRMMASK (1<<(SIGALRM-1))  // 警告(alarm)信号屏蔽位
#define KILLMASK (1<<(SIGKILL-1))  // 终止(kill)信号屏蔽位
#define INTMASK (1<<(SIGINT-1))  // 键盘中断(int)信号屏蔽位
#define QUITMASK (1<<(SIGQUIT-1)) //键盘退出(quit)信号屏蔽位
#define TSTPMASK (1<<(SIGTSTP-1))  // tty 发出的停止进程(tty stop)信号屏蔽位

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>
// 获取termios结构中3个模式标志集之一,或者用于判断一个标志集是否由置位标志
#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f)  // 本地模式标志
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f)  // 输入模式标志
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f)  // 输出模式标志
//取termios结构终端特殊(本地)模式标志集中的一个标志
#define L_CANON(tty)	_L_FLAG((tty),ICANON)  // 取规范模式标志
#define L_ISIG(tty)	_L_FLAG((tty),ISIG) //取信号标志
#define L_ECHO(tty)	_L_FLAG((tty),ECHO)  // 取回显字符标志
#define L_ECHOE(tty)	_L_FLAG((tty),ECHOE)  // 规范模式时取回显擦除标志
#define L_ECHOK(tty)	_L_FLAG((tty),ECHOK) // 规范模式时取KILL擦除当前行标志
#define L_ECHOCTL(tty)	_L_FLAG((tty),ECHOCTL) //取回显控制字符标志
#define L_ECHOKE(tty)	_L_FLAG((tty),ECHOKE) //规范模式时取KILL擦除行并回显标志
//取termios结构输入模式标志集中的一个标志
#define I_UCLC(tty)	_I_FLAG((tty),IUCLC) //取大写到小写转换标志
#define I_NLCR(tty)	_I_FLAG((tty),INLCR) //取换行符NL转回车符CR标志
#define I_CRNL(tty)	_I_FLAG((tty),ICRNL) //取回车符CR转换行符NL标志
#define I_NOCR(tty)	_I_FLAG((tty),IGNCR) //取忽略回车符CR标志
//取termios结构输出模式标志集中的一个标志
#define O_POST(tty)	_O_FLAG((tty),OPOST)  //取执行输出处理标志
#define O_NLCR(tty)	_O_FLAG((tty),ONLCR) //取换行符NL转回车换行符CR-NL标志
#define O_CRNL(tty)	_O_FLAG((tty),OCRNL) //取回车符CR转换行符NL标志
#define O_NLRET(tty)	_O_FLAG((tty),ONLRET) //取换行符NL执行回车功能的标志
#define O_LCUC(tty)	_O_FLAG((tty),OLCUC) //取小写转大写字符标志
//tty 数据结构的tty_table数组, 其中包含3个初始化项数据,分别时控制台、串口终端1和串口终端2的初始化数据
struct tty_struct tty_table[] = {
	{
		{ICRNL,		/* change incoming CR to NL */ // 将输入CR转为 NL
		OPOST|ONLCR,	/* change outgoing NL to CRNL */ // 将输出 NL 转 CRNL
		0, //控制模式标志集
		ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, // 本地模式标志
		0,		/* console termio */  // 线路规程, 0-tty
		INIT_C_CC}, //控制字符数组
		0,			/* initial pgrp */ // 所属初始进程组
		0,			/* initial stopped */  // 初始停止标志
		con_write,  //控制台写函数
		{0,0,0,0,""},		/* console read-queue */ // 控制台读缓冲队列
		{0,0,0,0,""},		/* console write-queue */ //控制台写缓冲队列
		{0,0,0,0,""}		/* console secondary queue */ //控制台辅助(第2)队列
	},{
		{0, /* no translation */ //输入模式标志集, 0 无需转换
		0,  /* no translation */ // 输出模式标志集, 0 无需转换
		B2400 | CS8,  // 本地控制标志集, 2400bps, 8为数据位
		0, //本地模式标志集
		0, // 线路规程, 0 tty
		INIT_C_CC}, //控制字符数组
		0, //所属初始进程组
		0, //初始停止标志
		rs_write, //串口1终端写函数
		{0x3f8,0,0,0,""},		/* rs 1 */ //串行终端1读缓冲队列结构初始值
		{0x3f8,0,0,0,""}, //串行终端1写缓冲队列结构初始值
		{0,0,0,0,""} //串行终端1辅助缓冲队列结构初始值
	},{
		{0, /* no translation */ //输入模式标志集 0 无需转换
		0,  /* no translation */ // 输出模式标志集 0 无需转换
		B2400 | CS8, //控制模式标志集, 2400bps, 8位数据位
		0, // 输入模式标志集 0 无需转换
		0, //输出模式标志集 0 无需转换
		INIT_C_CC}, // 控制字符数组
		0, //所属初始进程组
		0, //初始停止标志
		rs_write, //串口2终端写函数
		{0x2f8,0,0,0,""},		/* rs 2 */ //串行终端2读缓冲队列结构初始值
		{0x2f8,0,0,0,""}, // 串行终端2写缓冲队列结构初始值
		{0,0,0,0,""} // 串行终端2辅助缓冲队列结构初始值
	}
};

/*
 * these are the tables used by the machine code handlers.//汇编程序使用的缓冲队列结构地址表
 * you can implement pseudo-tty's or something by changing//通过修改这个表可以实现伪tty
 * them. Currently not done. //终端或其他类型终端
 */
struct tty_queue * table_list[]={ //tty读写缓冲队列结构地址表. 供rs_io.s汇编使用,用于取得读写缓冲队列地址
	&tty_table[0].read_q, &tty_table[0].write_q, //控制台终端读写队列地址
	&tty_table[1].read_q, &tty_table[1].write_q, //串行终端1读写队列地址
	&tty_table[2].read_q, &tty_table[2].write_q //串行终端2读写队列地址
	};
//tty终端初始化函数; 初始化串口终端和控制台终端.
void tty_init(void)
{
	rs_init(); //初始化串行中断程序和串行接口1和2
	con_init(); //初始化控制台终端
}
//tty键盘中断字符(^c)处理函数;向tty结构中指明的(前台)进程组中所有进程发送指定信号mask,
// 通常该信号是SIGINT, 参数: tty指定终端的tty结构指针; mask-信号屏蔽位
void tty_intr(struct tty_struct * tty, int mask)
{
	int i;
//首先检查终端进程组号. 如果tty所属进程组号<=0,则退出; 当pgrp=0时,表明进程时初始进程init
//它没有控制终端,因此不应该发出中断字符.
	if (tty->pgrp <= 0)
		return;
//描述任务数组,向tty指明的进程组(前台进程组)中所有进程发送指定信号. 即如果该项任务指针不空
//且组号=tty组号,则设置(发送)该任务指定的信号mask.
	for (i=0;i<NR_TASKS;i++)
		if (task[i] && task[i]->pgrp==tty->pgrp)
			task[i]->signal |= mask;
}
//如果队列缓冲区空则让进程进入可中断睡眠状态; queue指定队列的指针; 进程在取队列缓冲区中字符之前需要调用此函数加以验证
static void sleep_if_empty(struct tty_queue * queue)
{
//若当前进程没有信号要处理,且指定的队列缓冲区空,则让进程进入可中断睡眠状态,且让队列的进程等待
//指针指向该进程.	
	cli();
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->proc_list);
	sti();
}
//若队列缓冲区满则让进程进入可中断的睡眠状态; queue指定队列的指针; 进程在往队列缓冲区中写入
//字符前,需要调用此函数判断队列情况
static void sleep_if_full(struct tty_queue * queue)
{
//如果队列缓冲区不满则返回退出; 否则若进程没有信号需要处理,且队列缓冲区中空闲剩余区长度<128
//则让进程进入可中断睡眠状态,并让该队列的进程等待指针指向该进程	
	if (!FULL(*queue))
		return;
	cli();
	while (!current->signal && LEFT(*queue)<128)
		interruptible_sleep_on(&queue->proc_list);
	sti();
}
//等待按键; 如果控制台读队列缓冲区空,则让进程进入可中断睡眠状态;
void wait_for_keypress(void)
{
	sleep_if_empty(&tty_table[0].secondary);
}
//复制成规范模式字符序列; 根据终端termios结构中设置的各种标志,将指定tty终端队列缓冲区中的字符
//复制转换成规范模式(熟模式)字符并放在辅助队列(规范模式队列)中; tty 指定终端的tty结构指针
void copy_to_cooked(struct tty_struct * tty)
{
	signed char c;
//如果tty的读队列缓冲区不空且辅助队列缓冲区不满,则循环读取队列缓冲区中的字符,转换成规范模式
//后放入secondary缓冲区中,直到读队列缓冲区空或者辅助独立满为止. 在循环中,程序首先从读队列
//缓冲区尾指针处取一个字符,并把尾指针前移一个字符位置. 然后根据终端termios中输入模式标志
//集中设置的标志对字符进行处理.
	while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
		GETCH(tty->read_q,c); // 取一个字符到c,并迁移尾指针
//如果该字符是回车符CR(13),那么若回车转换行标志CRNL置位,则将字符转换尾换行符NL(10),否则如果
//忽略回车标志NOCR置位,则忽略该字符,继续处理其他字符.如果字符是换行符NL(10),且换行转回车标志
//NLCR置位,则将其转换尾回车符CR(13)
		if (c==13)
			if (I_CRNL(tty))
				c=10;
			else if (I_NOCR(tty))
				continue;
			else ;// 该else用于结束内部if语句
		else if (c==10 && I_NLCR(tty))
			c=13;
		if (I_UCLC(tty)) //如果大写转小写标志UCLC置位,则将该字符转换为小写字符
			c=tolower(c);
//如果本地模式标志集中规范模式标志CANON已置位,则对读取的字符进行以下处理. 首先如果该字符是
//键盘终止控制符KILL(^U),则对已输入的当前行执行删除处理,删除一行字符的循环过程如下: 如果tty
//辅助队列不空,且取出的辅助队列中最后一个字符不是换行符NL(10)且字符不是文件结束字符(^D),
//则循环执行下列代码: 如果本地回显标志ECHO置位,那么若字符是控制字符(值<32),则往tty写队列
//中放入擦除控制字符ERASE(^H),然后再放入一个擦除字符ERASE,且调用该tty写函数,把写入队列中
//的所有字符输出到终端屏幕上. 另外,因为控制字符在放入写队列时需要用2个字节表示(如^v),因此
//要求特别对控制字符多放入一个ERASE. 最后将tty辅助队列头指针后退1字节.
		if (L_CANON(tty)) {
			if (c==KILL_CHAR(tty)) {
				/* deal with killing the input line */ //删除输入行
				while(!(EMPTY(tty->secondary) ||
				        (c=LAST(tty->secondary))==10 ||
				        c==EOF_CHAR(tty))) {
					if (L_ECHO(tty)) { //若本地回显标志置位,控制字符要删2字节
						if (c<32)
							PUTCH(127,tty->write_q);
						PUTCH(127,tty->write_q);
						tty->write(tty);
					}
					DEC(tty->secondary.head);
				}
				continue; //继续读取队列中字符进行处理
			}
//如果该字符时删除控制字符ERASE(^H), 如果tty辅助队列空,或其最后一个字符时换行符NL,或文件结束
//符,则继续处理其他字符. 如果本地回显标志ECHO置位, 那么若字符时控制字符(值<32), 则往tty的写队列
//中放入擦除字符ERASE. 再放入一个擦除字符ERASE, 且调用该tty的写函数. 最后将tty辅助队列头指针
//后退1字节,继续处理其他字符
			if (c==ERASE_CHAR(tty)) {
				if (EMPTY(tty->secondary) ||
				   (c=LAST(tty->secondary))==10 ||
				   c==EOF_CHAR(tty))
					continue;
				if (L_ECHO(tty)) { //若本地回显标志置位
					if (c<32)
						PUTCH(127,tty->write_q);
					PUTCH(127,tty->write_q);
					tty->write(tty);
				}
				DEC(tty->secondary.head);
				continue;
			}
//如果字符时停止控制符,则只tty停止标志,停止tty输出,并继续处理其他字符.如果字符时开始字符
//则复位tty停止标志,恢复tty输出,并继续处理其他字符			
			if (c==STOP_CHAR(tty)) {
				tty->stopped=1;
				continue;
			}
			if (c==START_CHAR(tty)) {
				tty->stopped=0;
				continue;
			}
		} //当前字符的输入规范模式处理结束(L_CANON(tty));
//若输入模式标志集中ISIG标志zhiwei,表示终端键盘可以产生信号,则再收到控制字符INTR,QUIT,SUSP
//DSUSP时,需要为进程产生相应的信号. 如果该字符是键盘中断符(^C),则向当前进程之进程组中所有进程
//发送键盘中断信号,并继续处理下一个字符. 如果该字符是退出符(^\),则向当前进程之进程组中所有进程
//发送键盘退出信号,并继续处理下一字符.
		if (L_ISIG(tty)) {
			if (c==INTR_CHAR(tty)) { //若是^C,则发送中断信号
				tty_intr(tty,INTMASK);
				continue;
			}
			if (c==QUIT_CHAR(tty)) { //若是^\, 则发退出信号
				tty_intr(tty,QUITMASK);
				continue;
			}
		}
//如果该字符是换行符NL(10), 或文件结束符EOF(4),表示一行字符已处理完,则把辅助缓冲队列中当前含有
//字符行数值secondary.data +1, 如果再函数 tty_read()中取走一行字符,该值会-1
		if (c==10 || c==EOF_CHAR(tty))
			tty->secondary.data++;
//如果本地模式标志集中回显标志ECHO在置位状态,那么如果字符是换行符NL,则将换行符NL和回车符CR
//放入tty写队列缓冲区中; 如果字符是控制字符且回显控制置位,则将字符'^'和字符c+64放入tty写队列
//中(即会显示^C,^H等); 否则将该字符直接放入tty写缓冲队列中.最后调用该tty写操作函数.
		if (L_ECHO(tty)) {
			if (c==10) {
				PUTCH(10,tty->write_q);
				PUTCH(13,tty->write_q);
			} else if (c<32) {
				if (L_ECHOCTL(tty)) {
					PUTCH('^',tty->write_q);
					PUTCH(c+64,tty->write_q);
				}
			} else
				PUTCH(c,tty->write_q);
			tty->write(tty);
		}
		PUTCH(c,tty->secondary); //每次循环末将处理过的字符放入辅助队列中
	}
	wake_up(&tty->secondary.proc_list); //在退出循环后唤醒等待该辅助缓冲队列的进程(如果有的话)
}
//tty读函数; 从终端辅助缓冲队列中读取指定数量的字符,放到用户指定的缓冲区中. channel子设备号;
//buf用户缓冲区指针; nr 预读字节数; 返回已读字节数
int tty_read(unsigned channel, char * buf, int nr)
{
	struct tty_struct * tty;
	char c, * b=buf;
	int minimum,time,flag=0;
	long oldalarm;
//首先判断函数参数有效性,并让tty指针指向参数子设备号对应ttb_table表中的tty结构.本版本Linux
//内核终端只有3个子设备, 分别是控制台终端(0),串口终端1(1),串口终端2(2),任何>2的子设备号都是
//非法的. 需要读取的字节数当然也不能小于0
	if (channel>2 || nr<0) return -1;
	tty = &tty_table[channel];
//接着保存进程原定时值,根据VTIME、VMIN对应的控制字符数组值设置读字符操作超时定时值time和最少
//需要读取的字符个数minimum. 在非规范模式下,这两个是超时定时值,VMIN表示为了满足读操作而需要
//读取的最少字符个数; VTIME是一个1/10s计数计时值.
	oldalarm = current->alarm;  // 保存进程当前的报警定时值(滴答数)
	time = 10L*tty->termios.c_cc[VTIME]; //设置读操作超时定时值
	minimum = tty->termios.c_cc[VMIN]; //最少需要读取的字符个数
//根据time,minimum的数值设置最少需要读取的确切字符数和等待延时值. 如果设置了读超时定时值time
//但没有设置最少读取个数minimum(即=0), 那么将设置成在读到至少一个字符或定时超时后读操作立刻
//返回,所以需要置minimum=1. 如果进程原定时值=0或time加上当前系统时间值小于进程原定时值的话,
//则置重新设置进程定时置为(time+当前系统时间),并置flag标志. 另外如果这里设置的最少读取字符数
//> 欲读取的字符数,则令其等于此次欲读取的字符数nr.
//注意: 这里设置进程alarm值而导致收到一个SIGALRM信号并不会导致进程终止退出. 当设置进程的
//alarm时,同时会设置一个flag标志,当这个alarm超时时,虽然内核会向进程发SIGALRM信号,但这里会
//利用flag来判断到底时用户还是内核设置的alarm值,若flag!=0, 内核代码就会负责复位此时产生的
//SIGALRM信号, 因此这里设置的SIGALRM信号不会使当前进程无故终止退出. 这种方式有些繁琐,且
//容易出问题,因此以后内核数据结构中就特地使用了一个timeout变量来专门处理这种问题.
	if (time && !minimum) {
		minimum=1;
		if (flag=(!oldalarm || time+jiffies<oldalarm))
			current->alarm = time+jiffies;
	}
	if (minimum>nr)
		minimum=nr; //最多读取要求的字符数
//现在开时从辅助队列中循环取出字符并放到用户缓冲区buf中,当预读的字节数>0,则执行以下循环.
//在循环过程中,如果flag已设置(即进程原定时值=0,或time+当前系统时间值小于进程原定时值),
//且进程此时已收到定时报警信号,表明这里新设置的定时时间已到.于是复位进程的定时信号并中断循环
//如果flag没有置位(即进程原来设置过定时值且这里重新设置的定时值 > 原来设置的定时值)因而是收到
//了原定时的报警信号,或者flag已置位但当前进程此时收到了其他信号,则退出循环,返回0
	while (nr>0) {
		if (flag && (current->signal & ALRMMASK)) {
			current->signal &= ~ALRMMASK;
			break;
		}
		if (current->signal) //若是进程原定时到或收到其他信号
			break;
//如果辅助缓冲队列为空,或者设置了规范模式标志且辅助队列中字符行数为0以及辅助模式缓冲队列空闲
//空间 >20, 则让当前进程进入可中断睡眠状态,返回后继续处理. 由于规范模式时内核以行为单位为用户
//提供数据,因此在该模式下辅助队列中必须起码有一行字符可供取用,即secondary.data起码是1才行
//另外,由这里的LEFT()判断可知,即使辅助队列中还没有放入一行(即应该有一个回车符),但如果此时
//一行字符个数已经超过1024-20个, 那么内核也会立刻执行读取操作.
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		!tty->secondary.data && LEFT(tty->secondary)>20)) {
			sleep_if_empty(&tty->secondary);
			continue;
		}
//开始正式执行取字符操作,需读字符数nr依次递减,直到nr=0或者辅助缓冲队列为空. 在这个循环过程中
//首先取辅助缓冲队列字符c,且把缓冲队列尾指针tail向右移动一个字符位置,如果所取字符是文件结束符
//或换行符NL, 则把辅助缓冲队列中含有字符行数值-1, 如果该字符是文件结束符且规范模式标志成置位
//状态,则返回已读字符数,并退出.
		do {
			GETCH(tty->secondary,c);
			if (c==EOF_CHAR(tty) || c==10)
				tty->secondary.data--;//字符行数-1
			if (c==EOF_CHAR(tty) && L_CANON(tty))
				return (b-buf);
//否则说明现在还没有遇到文件结束符或正处于原始(非规范)模式.这种模式中,用户以字符流作为读取
//对象,也不识别其中的控制字符(如文件结束符). 于是将字符直接放入用户数据缓冲区buf中,并把预读
//字符数 -1, 此时如果预读字符数为0,则中断循环. 否则只要还没有取完预读字符数且辅助队列不空,就
//继续读取队列中的字符, 另外由于在规范模式下遇到换行符NL时,也应该退出,因此在第271行后应该再
//添上一条语句 if(c==10 && L_CANON(tty)) break;
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			} //line271
		} while (nr>0 && !EMPTY(tty->secondary));
//此时已经读取了nr个字符,或辅助队列以被取空,对于已经读取nr个字符的情况,无需做什么. 程序会自动
//退出该循环,恢复进程原定时值并作退出处理. 如果由于辅助队列中字符被取空而退出上面do循环,就需要
//根据终端termios控制字符数组的time设置的延时时间来确定是否要等待一段时间,如果超时定时值time不
//为0且规范模式标志没有置位(非规范模式), 那么:如果进程原定时值=0或者time+当前系统时间值 < 进程
//原定时值oldalarm, 则置重新设置进程定时值为 time+当前系统时间, 并置flag标志,为还需要读取的
//字符做好定时准备. 否则表明进程原定时时间要比等待字符读取的定时时间要早,可能没有等到字符到来进程
//原定时时间就到了,因此此时这里需要恢复进程的原定时置 oldalarm
		if (time && !L_CANON(tty))
			if (flag=(!oldalarm || time+jiffies<oldalarm))
				current->alarm = time+jiffies;
			else
				current->alarm = oldalarm;
//如果设置了规范模式标志,若已读到起码一个字符则中断循环. 否则若已读取数 >= 最少要求读取的字符
//数,则也中断循环
		if (L_CANON(tty)) {
			if (b-buf)
				break;
		} else if (b-buf >= minimum)
			break;
	}
//此时读取tty字符循环操作结束,因此让进程的定时值恢复原值,最后如果进程接收到信号且没有读取到
//任何字符,则返回错误(被中断), 否则返回已读字符数.
	current->alarm = oldalarm;
	if (current->signal && !(b-buf))
		return -EINTR;
	return (b-buf); //返回已读取的字符数
}
//tty写函数. 把用户缓冲区中的字符写入tty写队列缓冲区中; channel 子设备号; buf 缓冲区指针; 
//nr 写字节数, 返回值=已写字节数
int tty_write(unsigned channel, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;
//判断函数参数有效性,并让tty指针指向参数子设备号对应ttb_table表中的tty结构.本Linux内核
//终端只有3个子设备号. 所以任何大于2的子设备号都是非法的. 需要读取的字节数不能小于0
	if (channel>2 || nr<0) return -1;
	tty = channel + tty_table;
//字符设备是一个一个字符进行处理的,所以这里对于nr大于0时对每个字符进行循环处理.循环体中,如果
//此时tty写队列已满,则当前进程进入可中断的睡眠状态,如果当前进程有信号要处理,则退出循环体
	while (nr>0) {
		sleep_if_full(&tty->write_q);
		if (current->signal)
			break;
//当要写道字节数nr > 0 且tty写队列未满,则循环执行以下操作: 从用户数据缓冲区中取1字节c,
//如果终端输出模式标志集中的执行输出处理标志OPOST置位,则执行对字符的后续处理操作
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
//如果该字符是回车符'\r'(13)且回车符转换行标志OCRNL置位,则将该字符换成换行符\n,否则如果该
//字符是换行符\n且换行转回车功能标志ONLRET置位的话,则将该字符换为回车符\r
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
//如果该字符是换行符\n且回车标志cr_flag没有置位,但换行转回车-换行标志ONLCR置位的话,则将
//cr_flag标志置位,并将一回车符放入写队列中,然后继续处理下一个字符.如果小写转大写标志OLCUC
//置位,就将该字符转成大写字符
				if (c=='\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13,tty->write_q);
					continue;
				}
				if (O_LCUC(tty)) //小写转大写
					c=toupper(c);
			}
//把用户数据缓冲指针b前移1字节,欲写字节数-1字节,复位cr_flag 标志,并将该字节放入tty写队列中
			b++; nr--;
			cr_flag = 0;
			PUTCH(c,tty->write_q);
		}
//若要求的字符全部写完,或写队列已满,则程序退出循环. 此时会调用对应tty写函数,把写队列缓冲
//区中的字符显示在控制台屏幕上,或通过串行端口发送出去. 如果当前处理的tty是控制台终端,
//那么tty->write()调用的是con_write(), 如果tty是串行终端,则tty->write()调用的是
//rs_write(), 若还有字节要写,则等待写队列中字符取走,所以这里调用调度程序,先去执行其他任务
		tty->write(tty);
		if (nr>0)
			schedule();
	}
	return (b-buf); //最后返回写入的字节数
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
//tty中断处理调用函数, 字符规范模式处理. tty 指定的tty终端号(0,1,2), 将指定tty终端队列
//缓冲区中的字符复制或转换成规范模式字符并放在辅助队列中. 该函数会在串口读字符中断和键盘中断中被调用
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table+tty);
}
//字符设备初始化, 为以后扩展作准备
void chr_dev_init(void)
{
}
