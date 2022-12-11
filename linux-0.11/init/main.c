/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__  // 定义__LIBRARY__宏是为了包括定义在 unistd.h 中的内嵌汇编代码等信息
// .h 头文件所在的默认目录是 include, 则在代码中就不用明确指明其位置. 如果不是 unix 的标准头文件
// 则需要指明所在的目录, 并用双引号括住. unistd.h 是标准符号常数与类型文件.
// 其中定义了各种符号常数和类型, 并声明了各种函数, 如果还定义了符号__LIBRARY__, 则会包括系统调用号和
// 内嵌汇编代码 syscall0()等
#include <unistd.h>
#include <time.h>  // 时间类型头文件. 其中最主要定义了tm结构和一些有关时间的函数原型

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
 * linux在内核空间创建进程时不使用写时复制技术, main()在移动到用户模式(到任务0)后执行内嵌方式的fork()和 pause(), 因此可以保证不使用
 * 任务0的用户栈. 在执行 moveto_user_mode() 后, 本程序main() 就以任务0的身份在运行了. 任务0是所有将创建子进程的父进程. 当它创建一个
 * 子进程时(init 进程), 由于任务1代码属于内核空间, 因此没有使用写时复制, 此时任务0的用户栈就是任务1的用户栈, 即它们共同使用一个栈空间
 * 因此希望 main.c 在运行任务0的环境下时不要有对堆栈的任何操作, 以免弄乱堆栈. 而在再次执行fork()并执行过 execve()后, 被加载程序已
 * 不属于内核空间, 因此可以使用写时复制技术了
 * 
 * _syscall0() 是unistd.h 中的内嵌宏代码, 以嵌入汇编的形式调用 linux 的系统调用中断 0x80, 该中断是所有系统调用的入口. 该条语句
 * 实际上是 int fork() 创建进程系统调用. _syscall0中最后的0表示无参数, 同理1表示1个参数.
*/
static inline _syscall0(int,fork)  // int fork 系统调用
static inline _syscall0(int,pause)  // int pause 系统调用: 暂停进程的执行, 直到收到一个信号
static inline _syscall1(int,setup,void *,BIOS) // int setup(void *BIOS) 系统调用, 仅用于Linux初始化
static inline _syscall0(int,sync)  // int sync() 系统调用 更新文件系统

#include <linux/tty.h> // tty 头文件, 定义了有关 tty_io, 串行通信方面的参数、常数.

// 调度程序头文件, 定义了任务结构 task_struct、第1个初始任务的数据, 还有一些以宏的形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序
#include <linux/sched.h>
// head 头文件, 定义段描述符的结构, 和几个选择符常量
#include <linux/head.h>
// 系统头文件, 以宏的形式吃那个月了许多有关设置或修改描述符、中断门的嵌入式汇编子程序
#include <asm/system.h>
#include <asm/io.h> //io头文件, 以宏的嵌入汇编程序形式定义对io端口操作的函数

#include <stddef.h>  // 标准定义头文件, 定义了NULL, offsetof(TYPE, MEMBER)
// 标准参数头文件, 以宏的形式定义变量参数列表, 主要说明了一个类型(va_list)和三个宏(va_start, va_arg, va_end), vsprintf,
// vprintf, vfprintf
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>  // 文件控制头文件, 用于文件及其描述符的操作控制常数符号的定义
#include <sys/types.h>  // 类型头文件, 定义了基本的系统数据类型

#include <linux/fs.h>  // 文件系统头文件, 定义文件表结构(file, buffer_head, m_inode)

static char printbuf[1024];  // 静态字符串数组, 用作内核显示信息的缓存

extern int vsprintf();  // 送格式化输出到一个字符串中 vsprintf.c
extern void init(void);  // 初始化函数原型
extern void blk_dev_init(void);  // 块设备初始化子程序 blk_drv/ll_rw_blk.c
extern void chr_dev_init(void);  // 字符设备初始化 chr_drv/tty_io.c
extern void hd_init(void);  // 硬盘初始化程序 blk_drv/hd.c
extern void floppy_init(void);  // 软驱初始化程序 blk_drv/floppy.c
extern void mem_init(long start, long end);  // 内存管理初始化 mm/memory.c
extern long rd_init(long mem_start, int length);  // 虚拟盘初始化 blk_drv/ramdisk.c
extern long kernel_mktime(struct tm * tm);  // 计算系统开机启动时间(秒s)
extern long startup_time;  // 内核启动时间(开机时间)秒

/*
 * This is set up by the setup-routine at boot-time
 * 下面三行数据是在内核引导期间由 setup.s 程序设置的
 * 分别将指定的线性地址强行转换为给定数据类型的指针, 并获取指针所指内容. 由于内核代码段被映射到从物理地址0x0开始的地方, 因此这些线性
 * 地址正好也是对应的物理地址
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)  // 1M之后的扩展内存大小(Kb)
#define DRIVE_INFO (*(struct drive_info *)0x90080)  // 硬盘参数表的32字节内容
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)  // 根文件系统所在设备号

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
// 这段宏读取CMOS时钟信息, outb_p, inb_p 是 include/asm/io.h 中定义的端口输入输出宏
// 0x70 是写地址端口号, 0x80|addr 是要读取的CMOS内存地址; 0x71 是读数据端口号
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

// 定义宏, 将BCD码转换成二进制数值, BCD码利用半个字节(4bit)表示一个10进制数, 因此一个字节表示2个10进制数. (val)&15取BCD表示的10
// 进制个位数, (val)>>4取BCD表示的10进制十位数, 再乘以10, 因此最后两者相加就是一个字节BCD码的实际二进制数值.
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

// 该函数读取CMOS实时时钟信息作为开机时间, 并保存到全局变量 startup_time 中, kernel_mktime用于计算从1970.1.1日0时其到开机当日
// 经过的秒数, 作为开机时间
static void time_init(void)
{
	struct tm time;  // 时间结构体 tm 定义在 include/time.h 中

	do {
		time.tm_sec = CMOS_READ(0);  // 当前时间秒值(都是BCD码)
		time.tm_min = CMOS_READ(2);  // 分钟
		time.tm_hour = CMOS_READ(4); // 小时
		time.tm_mday = CMOS_READ(7);  // 一月中的当天日期
		time.tm_mon = CMOS_READ(8);  // 当前月份(1-12)
		time.tm_year = CMOS_READ(9);  // 当前年份
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);  // 转换成二进制
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;  // tm_mon 中月份范围是 0-11, 因此需要减1
	startup_time = kernel_mktime(&time);  // 计算开机时间 kernel/mktime.c
}

static long memory_end = 0;  // 机器具有的物理内存容量, 字节数
static long buffer_memory_end = 0;  // 高速缓冲区末端地址
static long main_memory_start = 0;  // 主内存(用于分页)开始的位置

struct drive_info { char dummy[32]; } drive_info;  // 用于存放硬盘参数表示信息

// 内核初始化主程序. 初始化结束后, 将以任务0(idel任务即空闲任务)的身份运行
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
/*
保存根设备号 ROOT_DEV; 高速缓存末端地址 buffer_memory_end; 机器内存数 memory_end; 主内存开始地址 main_memory_start;
*/
 	ROOT_DEV = ORIG_ROOT_DEV;  // ROOT_DEV 已在前面包含进的include/linux/fs.h 文件中被声明为 extern int
 	drive_info = DRIVE_INFO;  // 复制0x90080处的硬盘参数表
	memory_end = (1<<20) + (EXT_MEM_K<<10);  // 内存大小=1M+扩展内存(k) * 1024 字节
	memory_end &= 0xfffff000;  // 忽略不到4k的内存数
	if (memory_end > 16*1024*1024)  // 如果内存量超过16M, 则按16M计算
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) // 如果内存 > 12M, 则设置缓冲区末端=4M
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)  // 若内存 > 6M, 则设置缓冲区末端=2M
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;  // 否则设置缓冲区末端 = 1M
	main_memory_start = buffer_memory_end;  // 主内存开始位置 = 缓冲区末端
#ifdef RAMDISK  // 如果在makefile中定义了内存虚拟盘符号 RAMDISK, 则初始化虚拟盘. 此时主内存将减少
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	// 以下是内核进行所有方面的初始化工作.
	mem_init(main_memory_start,memory_end);  // 主内存区初始化
	trap_init();  // 陷阱门(硬件中断向量)初始化
	blk_dev_init();  // 块设备初始化
	chr_dev_init();  // 字符设备初始化
	tty_init();  // tty初始化
	time_init();  // 设置开机启动时间 -> startup_time
	sched_init();  // 调度程序初始化(加载任务0的tr,ldtr)
	buffer_init(buffer_memory_end); // 缓冲管理初始化, 建内存链表等
	hd_init();  // 硬盘初始化
	floppy_init();  // 软驱初始化
	sti();  // 所有初始化工作都做完了, 开启中断

	// 通过在堆栈中设置的参数, 利用中断返回指令启动任务0执行
	move_to_user_mode();  // 移到用户模式下执行
	if (!fork()) {		/* we count on this going ok */
		init();  // 在新建的子进程(任务1)中执行
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 * 对于任何其他任务, pause() 将意味着必须等待收到一个信号才会返回就绪态. 但任务0是唯一例外情况. 因为任务0在任何空闲时间里都会被
 * 激活(当没有其他任务在运行时), 因此对于任务0 pause()意味我们返回来查看是否有其他任务可以运行, 如果没有的话就回到这里, 一直循环
 * 执行pause
 * 
 * pause() 系统调用会把任务0转换成可中断等待状态, 再执行调度函数. 但调度函数只要发现系统中没有其他任务可以运行时就切换到任务0,
 * 而不依赖任务0的状态
 */
	for(;;) pause();
}

// 产生格式化信息并输出到标准设备 stdout(1), 即屏幕上显示, *fmt指定输出将采用的格式, 该子程序正好是 vsprintf 如何使用的一个例子
// 该程序使用 vsprintf() 将格式化的字符串放入 printbuf 缓冲区中, 然后用write() 将缓冲区内容输出到标准设备中.
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 读取并执行 /etc/rc 文件时所使用的命令行参数和环境参数
static char * argv_rc[] = { "/bin/sh", NULL };  // 调用执行程序时参数的字符串数组
static char * envp_rc[] = { "HOME=/", NULL };  // 调用执行程序时的环境字符串数组

// 运行登录shell时所使用的命令行参数和环境参数; argv[0]中的第一个字符'-' 是传递给shell程序sh的一个标志, 通过识别该标志, sh程序
// 会作为登录shell执行, 其执行过程与在shell提示符下执行sh不一样
static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

/*
main()中以及进行了系统初始化, 包括内存管理、各种硬件设备和驱动程序. init()函数运行在任务0第1次创建子进程(任务1)中, 它首先对第一个将要
执行的程序(shell) 的环境进行初始化, 然后以登录shell方式加载该程序并执行.
*/
void init(void)
{
	int pid,i;

	// setup 是一个系统调用, 用于读取硬盘参数包括分区信息并加载虚拟盘(若存在的话)和安装根文件系统设备. 该函数对应sys_setup(),
	setup((void *) &drive_info);  // drive_info 结构中是两个硬盘参数表

	// 以读写访问方式打开设备/dev/tty0, 它对应中断控制台. 由于是第一次打开文件操作, 因此产生的文件句柄号(文件描述符)是0.
	// 该句柄是unix类操作系统默认的控制台标准输入句柄 stdin, 这里再把它以读和写的方式分别打开是为了复制产生标准输出(写)句柄stdout
	// 和标准出错输出句柄stderr, 函数前面的 void 用于表示强制函数无需返回值.
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);  // 复制句柄, 产生句柄号1--stdout标准输出设备
	(void) dup(0);  // 复制句柄, 产生句柄号2 -- stderr

	// 打印缓冲区块数和总字节数, 每块1024字节, 以及主内存区空闲字节数
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

	// fork() 用于创建一个子进程(任务2). 对于被创建的子进程, fork()将返回0值, 对于原进程(父进程)则返回子进程的进程号pid, 所以第一个
	// if语句中是子进程执行的内容, 该子进程关闭了句柄0(stdin), 以只读方式打开 etc/rc 文件, 并使用execve()函数将进程自身替换替换成
	// /bin/sh, 然后执行/bin/sh, 所携带的参数和环境变量分别由 argv_rc 和 envp_rc 数组给出. 关闭句柄0并立刻打开 /etc/rc 文件的目的
	// 是把标准输入stdin重定向到 /etc/rc 文件, 这样shell程序/bin/sh 就可以运行 rc 文件中设置的命令, 由于这里sh的运行方式是非交互
	// 式的, 因此在执行完 rc 文件中的命令后立刻退出. 进程2就随之结束.
	// 函数_exit()退出时的出错码: 1-操作未许可; 2-文件或目录不存在
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);  // 打开文件失败, 则退出(lib/_exit.c)
		execve("/bin/sh",argv_rc,envp_rc);  // 替换成 /bin/sh 程序并执行, 若execve执行失败则退出  //line183
		_exit(2);
	}

	// 父进程执行的语句, wait()等待子进程停止或终止. 返回值是子进程的进程号pid, 即父进程等待子进程的结束, &i 是存放返回状态信息的
	// 位置, 如果wait()返回值不等于子进程号, 则继续等待.
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;

	// 执行都这里说明刚创建的子进程以及停止了, 然后接着再创建一个子进程, 如果出错则显示错误信息并继续执行. 对于锁创建的子进程将关闭
	// 所有以前还遗留的句柄, 新创建一个会话并设置进程组号. 然后重新打开 dev/tty0 作为stdin, 并复制成 stdout, stderr. 再次执行
	// /bin/sh, 这次使用的参数和环境数组另选了一套. 然后父进程再次运行wait()等待. 如果子进程又停止了, 则在标准输出显示错误信息, 
	// 然后继续重试下去. wait的另一个作用是处理孤儿进程. 如果一个进程的父进程先终止了, 那么这个进程的父进程就会被设置为这里的 init
	// 进程, 并由init进程负责释放一个已终止进程的任务数据结构等资源
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid(); // 创建一个新的会话期,
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));  //line200
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync(); // 同步操作, 刷新缓冲区
	}
	_exit(0);	/* NOTE! _exit, not exit() */
	// _exit(), exit()都用于正常终止一个函数, 但 _exit()直接是一个 sys_exit()系统调用, exit() 则通常是普通函数库中的一个函数,
	// 它会先执行一些清除操作, 如: 调用执行各终止处理程序、关闭所有标准IO等, 然后调用 sys_exit
}
