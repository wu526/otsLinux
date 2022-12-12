/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
// 调度头文件, 定义了任务结构 task_struct, 第1个初始任务的数据. 一些以宏形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序
#include <linux/sched.h>
#include <linux/kernel.h>  // 内核头文件, 含有一些内核常用函数的原型定义
#include <linux/sys.h>  // 系统调用头文件, 含有72个系统调用c函数处理程序, 以sys_开头
#include <linux/fdreg.h>  // 软驱头文件, 含有软盘控制器参数的一些定义
#include <asm/system.h>  //系统头文件, 定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include <asm/io.h>  // io头文件, 定义硬件端口输入、输出宏汇编语句
#include <asm/segment.h>  //段操作头文件 定义了有关段寄存器操作的嵌入式汇编

#include <signal.h>  // 信号头文件, 定义信号符号常量, sigaction 结构, 操作函数原型

// 该宏取信号nr在信号位图中对应位的二进制数值, 信号编号1-32, 如信号5的位图数值等于 1<<(5-1) = 16 = 00010000b
#define _S(nr) (1<<((nr)-1))
// 除了SIGKILL, SIGSTOP 信号以外其他信号都是可阻塞的
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

// 内核调试函数, 显示任务号 nr 的进程号、进程状态和内核堆栈空闲字节数(大约)
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i=0;
	while (i<j && !((char *)(p+1))[i])  // 检测指定任务数据结构以后等于0的字节数
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

// 显示所有任务的任务号、进程号、进程状态和内核堆栈空闲字节数
// NR_TASKS 是系统能容纳的最大进程数, 定义在 include/kernel/sched.h
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}

// PC 机8253定时芯片的输入时钟频率约为 1.193180MHz, linux希望定时器发出中断的频率是 100Mhz, 即每10ms发一次时钟中断, 因此这里的
// LATCH 是设置 8253 芯片的初值.
#define LATCH (1193180/HZ)

extern void mem_use(void);  // 没有任何地方定义和引用该函数??

extern int timer_interrupt(void);  // 时钟中断处理程序 kernel/system_call.s
extern int system_call(void);  // 系统调用中断处理函数 kernel/system_call.s

// 每个任务在内核态运行时都有自己的内核态堆栈, 这里定义了任务的内核态堆栈结构. 定义联合数据(任务结构成员和stack字符数组成员)
union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];  // 因为一个任务的数据结果与其内核态堆栈在同一页内存中, 所以从堆栈段寄存器ss可以获得其数据段选择符
};

static union task_union init_task = {INIT_TASK,};  // 定义初始任务的数据 sched.h

/*
从开机开始算起的滴答数时间值全局变量(10ms/滴答), 系统时钟中断每发生一次即一个滴答. volatile(易改变,不稳定)限定词的含义是向编译器指明变量的
内容可能会由于被其他程序修改而变化, 通常在程序中申明一个变量时, 编译器会尽量把它存放在通用寄存器中, 当cpu把其值放到寄存器后一般就不会再关心
该变量对应内存位置中的内容, 若此时其他程序(如内核程序或一个中断过程)修改了内存中该变量的值, 寄存器中的值不会随之更新. 为了解决这种情况,
就创建了 volatile 限定符, 让代码在引用该变量时一定要从指定内存位置中取得其值. 这里就是要求gcc不要对jiffies 进行优化处理, 也不要挪动
位置, 且需要从内存中取得其值, 因为时钟中断处理过程等程序会修改它的值
*/
long volatile jiffies=0;
long startup_time=0; // 开机时间, 从1970开始的秒数
struct task_struct *current = &(init_task.task);  // 当前任务指针(初始化指向任务0)
struct task_struct *last_task_used_math = NULL; // 使用过协处理器任务的指针

struct task_struct * task[NR_TASKS] = {&(init_task.task), };  // 定义任务指针数组

/*
定义用户栈, 共1k项, 容量4k字节. 在内核初始化操作过程中被用作内核栈, 初始化完成后将被用作任务0的用户态堆栈. 在运行任务0之前它是内核栈,
以后用作任务0和1的用户态栈.
*/
long user_stack [ PAGE_SIZE>>2 ] ;

/*
设置堆栈ss:esp, ss(即b, 高位)被设置为内核数据段选择符0x10, 指针 esp(*a)在user_stack 数组最后一项后面. 这是因为intel cpu
执行堆栈操作时先递减堆栈指针sp值, 然后在 sp指针处保存入栈内容
*/
struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 * 将当前协处理器内容保存到老协处理器状态数组中, 并将当前任务的协处理器内容加载进协处理器
 */
// 当任务被调度交换后, 该函数用以保存原任务的协处理器状态(上下文)并恢复新调度进来的当前任务的协处理器执行状态
void math_state_restore()
{
	// 如果任务没有变则返回(上一个任务就是当前任务). 这里上一个任务是指刚被交换出去的任务
	if (last_task_used_math == current)
		return;

	// 在发送协处理器命令之前要先发wait指令, 如果上个任务使用了协处理器, 则保存其状态
	__asm__("fwait");
	if (last_task_used_math) {
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}

	// 现在 last_task_used_math 指向当前任务, 以备当前任务被交换出去时使用. 此时如果当前任务用过协处理器, 则恢复其状态. 否则的话
	// 说明是第一次使用, 于是就向协处理发初始化命令, 并设置使用协处理器标志
	last_task_used_math=current;
	if (current->used_math) {
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {
		__asm__("fninit"::);  // 向协处理器发初始化命令
		current->used_math=1;  // 设置已使用协处理器标志
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;  // 任务结构指针的指针

/* check alarm, wake up any interruptible tasks that have got a signal */
// 检测alarm进程的报警定时值, 唤醒任何已得到信号的可中断任务.
// 从任务数组中最后一个任务开始循环检测 alarm, 在循环时跳过空指针项.
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			// 如果设置过任务的定时值alarm, 且已经过期(alarm < jiffies), 则在信号位图中置SIGALRM
			// 信号, 即向任务发送 SIGALARM 信号. 然后清 alarm. 该信号的默认操作时终止进程.
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
			// 如果信号位图中除被阻塞的信号外还有其他信号, 且任务处于可中断状态, 则置任务
			// 为就绪状态. _BLOCKABLE & (*p)->blocked 用于忽略被阻塞的信号, SIGKILL和
			// SIGSTOP 不能被阻塞
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;  // 置为就绪状态(可执行)
		}

/* this is the scheduler proper: */
// 调度程序的主要部分
	while (1) {  // line124
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
		// 从任务数组的最后一个任务开始循环处理, 并跳过不含任务的数组槽.比较每个就绪状态任务的
		// counter(任务运行时间的递减滴答计数)值, 哪一个值大(即运行时间还不长), next就指向它
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		// 如果比较后有counter值不等于0, 或者系统中没有一个可运行的任务存在(此时c仍然为-1,next=0)
		// 则退出line124 开始的循环, 执行 line141上的任务切换操作. 否则就根据每个任务的优先权值,
		// 更新每一个任务的 counter 值, 然后回到 line124重新比较. counter=counter/2 + priority
		// 这里的计算过程不考虑进程的状态.
		if (c) break;
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	// 将当前任务指针 current 指向任务号为 next 的任务, 并切换到该任务中运行. next最开始
	// 被初始化为0, 因此若系统没有任何其他任务可运行时, 则next=0, 因此调度函数会在系统空闲
	// 时执行任务0, 此时任务0仅执行 pause()系统调用, 并又会调用本函数.
	switch_to(next);  // 切换任务号为next的任务,并运行 line141
}

// pause 系统调用. 转换当前任务的状态为可中断的等待状态并重新调度.
// 该系统调用将导致进程进入睡眠状态, 直到收到一个信号. 该信号用于终止进程或使进程调用一个
// 信号捕获函数, 只有当捕获了一个信号且信号捕获处理函数返回, pause()才会返回. 此时pause()
// 返回值应该是 -1, 且 errno 被置为 EINTR. 这里还没有完全实现, 直到0.95版
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

// 将当前任务置为不可中断的等待状态, 并让睡眠队列头指针指向当前任务. 只有明确的唤醒时才会
// 返回. 该函数提供了进程与中断处理程序之间的同步机制. 函数参数p 是 等待任务队列头指针.
// 指针是含有一个变量地址的变量. 由于 *p 指向的目标(这里是任务结构)会改变, 因此为了能修改
// 调用该函数程序中原来就是指针变量的值, 就需要传递二级指针.
void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

// 若指针无效, 则退出. (指针所指的对象可以是NULL, 但指针本身不应该是0). 如果当前任务是
// 任务0, 则死机. 因为任务0的运行不依赖自己的状态, 所以内核代码把任务0置为睡眠状态无意义
	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");

	// 让tmp指向已经在等待队列上的任务(如果有的话), 如: inode->i_wait, 且将睡眠队列头的
	// 等待指针指向当前任务. 这样就把当前任务插入到了 *p 的等待队列中, 然后将当前任务置为
	// 不可中断的等待状态, 并指向重新调度
	tmp = *p;
	*p = current;
	current->state = TASK_UNINTERRUPTIBLE;
	schedule();

	// 只有当这个等待任务被唤醒时, 调度程序才又返回到这里, 表示本进程已被明确的唤醒(就绪)
	// 既然大家都在等待同样的资源, 那么在资源可用时, 就有必要唤醒所有等待该资源的进程
	// 该函数嵌套调用也会嵌套唤醒所有等待该资源的进程. 这里嵌套调用是指当一个进程调用了
	// sleep_on() 后就会在该函数中被切换掉, 控制权转移到其他进程中. 此时若有进程也需要
	// 使用同一资源, 那么也会使用同一个等待队列头指针作为参数调用 sleep_on()函数, 且也会
	// 陷入 该函数而不返回. 只有当内核某处代码以队列头指针作为参数 wake_up了该队列,
	// 那么当系统切换去执行头指针所指的进程A时, 该进程才会执行 line163, 把队列后一个进程
	// B 置位就绪状态(唤醒). 当轮到B进程时, 它也才可能继续执行line163, 若它后面还有等待的
	// 进程C, 那么会把C唤醒 等.
	if (tmp)  // 若在其前还存在等待的任务, 则也将其置为就绪状态(唤醒) line163
		tmp->state=0;
}

// 将当前任务置为可中断的等待状态, 并放入 *p 指定的等待队列中.
void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	// 若指针无效, 则退出.
	if (!p)
		return;
	// 如果当前任务是任务0, 则死机(impossible)
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");

	// 让 tmp 指向已经在等待队列上的任务(如果有的话), 如: inode->i_wait; 且将睡眠队列头
	// 的等待指针指向当前任务. 这样就把任务插入到 *p 的等待队列中, 然后将当前任务置为可中断
	// 的等待状态, 并执行重新调度
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();

	// 只有当这个任务被唤醒时, 程序才又会返回到这里. 表示进程已被明确的唤醒并执行. 如果等待
	// 队列中还有等待任务, 且队列头指针所指向的任务不是当前任务时, 则将该等待任务置为可运行
	// 的就绪状态, 且重新指向调度程序. 当指针 *p 所指向的不是当前任务时, 表示在当前任务被
	// 放入队列后, 又有新的任务被插入等待队列前部. 因此先唤醒它们, 而让自己等待. 等待这些
	// 后续进入队列的任务被唤醒执行时来唤醒本任务. 于是去执行重新调度.
	if (*p && *p != current) {
		(**p).state=0;
		goto repeat;
	}

	// 这句有误, 应该时 *p = tmp, 让队列头指针指向其余等待任务, 否则在当前任务之前插入
	// 等待队列的任务均被抹掉了, 同时也需要删除 line192 的语句 ??? #TODO
	*p=NULL;
	if (tmp)
		tmp->state=0;
}

// 唤醒 *p 指向的任务. *p 是任务等待队列头指针, 由于新等待任务是插入在等待队列头指针处
// 的, 因此唤醒的是最后进入等待队列的任务.
void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(**p).state=0;  // 设为就绪状态(可运行)
		*p=NULL;  // line192
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
// 在阅读这段代码前可以先看一下块设备中有关软盘驱动程序 floppy.c 的说明.
// 时间单位: 1个滴答=10ms=1/100秒

// 存放等待软驱马达启动到正常转速的进程指针. 数组索引0-3分别对应软驱A-D
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
// 存放各软驱马达启动所需要的滴答数, 程序中默认启动时间为50个滴答(0.5秒)
static int  mon_timer[4]={0,0,0,0};
// 存放各软驱在马达停转之前需维持的时间, 程序中设定为10000个滴答(100s)
static int moff_timer[4]={0,0,0,0};
// 对应软驱控制器中当前数字输出寄存器. 该寄存器每位定义如下:
/*
位7-4: 分别控制驱动器D-A马达的启动, 1启动,0关闭
位3: 1-允许DMA和中断请求; 0-禁止DMA和中断请求
位2: 1-启动软盘控制器; 0-恢复软盘控制器
位1-0: 00-11用于选择控制的软驱 A-D
*/
unsigned char current_DOR = 0x0C; // 初值为:允许DMA和中断请求,启动FDC

// 指定软驱启动到正常运转状态所需等待时间
// nr-软驱号(0-3), 返回值为滴答数
// 局部变量 selected 是选中软驱标志(blk_drv/floppy.c). mask 是所选软驱对应的数字输出
// 寄存器中启动马达bit位. mask 高4位是各软驱启动马达标志.
int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

// 系统最多有4个软驱. 首先预先设置好指定软驱 nr 停转之前需要经过的时间(100s), 然后取当前
// DOR 寄存器值到临时变量 mask 中, 并把指定软驱的马达启动标志置位
	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	// 如果当前没有选择软驱, 则首先复位其他软驱的选择位, 然后置指定软驱选择位.
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	// 如果数字输出寄存器的当前值与要求的值不同, 则向FDC数字输出端口输出新值(mask)
	// 且如果要求启动的马达还没有启动, 则置相应软驱的马达启动定时器值(HZ/2=0.5s或50个滴答)
	// 若已经启动, 则再设置启动定时为2个滴答, 能满足下面 do_floppy_timer()中先递减后判断
	// 的要求. 执行本次定时代码的要求即可. 此后更新当前数字输出寄存器 current_DOR
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();  // 开启中断
	return mon_timer[nr];  // 返回启动马达所需要的时间值
}

// 等待指定软驱马达启动所需的一段时间, 然后返回. 设置指定软驱的马达启动到正常转速所需要的
// 延时, 然后睡眠等待, 在定时中断过程中会一直递减这里设定的延时值. 当延时到期, 就会唤醒
// 这里的等待进程
void floppy_on(unsigned int nr)
{
	cli();
	// 如果马达启动定时还没到, 就一直把当前进程置为不可中断睡眠状态并放入等待马达运行的队列中
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

// 置关闭相应软驱马达停转定时器3s
// 若不使用该函数明确关闭指定的软驱mad, 则在马达开启100s后也会被关闭
void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

// 软盘定时处理子程序. 更新马达启动定时值和马达关闭停转计时值. 该子程序会在时钟定时中断
// 过程中被调用, 因此系统每经过一个滴答就会被调用一次, 随时更新马达开启或停转定时器的值
// 如果某个马达停转定时到, 则将该数字输出寄存器马达启动位复位.
void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))  // 如果不是DOR指定的马达则跳过
			continue;
		if (mon_timer[i]) {  // 如果马达启动定时到 则唤醒进程
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {  // 如果马达停转定时到则复位相应马达启动位,且更新数字输出寄存器
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;  // 马达停转计时递减
	}
}

// 定时器代码, 最多64个定时器
#define TIME_REQUESTS 64

// 定时器链表结构和定时器数组. 该定时器链表用于共软驱关闭马达和启动马达定时操作. 这种类型
// 定时器类似现代 linux 系统中的动态定时器, 仅供内核使用
static struct timer_list {
	long jiffies;  // 定时滴答数
	void (*fn)();  // 定时处理程序
	struct timer_list * next;  // 链接指向下一个定时器
} timer_list[TIME_REQUESTS], * next_timer = NULL;  // next_timer 定时器队列头指针

// 添加定时器. 输入参数为指定的定时值(滴答数)和相应的处理程序指针.
// 软盘驱动程序(floopy.c) 利用该函数执行启动或关闭马达的延时操作
// *fn 定时时间到时执行的函数, jiffies 以10ms记的滴答数
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

// 如果定时处理指针为空, 则退出
	if (!fn)
		return;
	cli();
	// 如果定时值 <=0, 则立刻调用其处理程序, 且该定时器不加入链表中
	if (jiffies <= 0)
		(fn)();
	else {
		// 从定时器数组中找一个空闲项
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
		// 如果已经用完了定时器数组, 则系统崩溃. 否则向定时器数组结构填入相应信息, 并链入链表头
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
// 链表项按定时值从小到大排序, 在排序时减去排在前面需要的滴答数, 这样在处理定时器时只要
// 看链表头的第一项的定时是否到期即可. #TODO: 如果新插入的定时器值小于原来头一个定时器值
// 时, 则不会进入循环中, 但此时还是应该将紧随其后面的一个定时器值减去新的第1个的定时值??
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}

// 时钟中断C函数处理程序, 在 system_call.s 中 _timer_interrupt 被调用. 参数 cpl 是当前
// 特权级0或3, 是时钟中断发生时正在被执行的代码选择符中的特权级.
// cpl=0表示中断发生时正在执行内核代码; cpl=3中断发生时正在执行用户代码.
// 对于一个进程由于执行时间片用完时, 则进行任务切换, 并执行一个计时更新工作
void do_timer(long cpl)
{
	extern int beepcount;  // 扬声器发声时间滴答数(kernel/chr_drv/console.c)
	extern void sysbeepstop(void);  // 关闭扬声器(kernel/chr_drv/console.c)

// 如果发声计数次数到则关闭发声. 向0x61口发送命令, 复位位0和1. 位0控制 8253, 计数器2
// 的工作, 位1控制扬声器
	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

// 如果当前特权级为0, 则将内核代码运行时间 stime 递增
	if (cpl)
		current->utime++;  // 用户程序在工作, utime增加
	else
		current->stime++;

// 如果有定时器存在, 则将链表第1个定时器的值 -1, 如果已经等于0则调用相应的处理程序,并
// 将该处理程序指针置空, 然后去掉该项定时器. next_timer 是定时器链表的头指针.
	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);  // 插入函数指针定义
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();  // 调用处理函数
		}
	}
// 如果当前软盘控制器 FDC 的数字输出寄存器中马达启动位有置位, 则执行软盘定时程序
	if (current_DOR & 0xf0)
		do_floppy_timer();
// 如果进程运行时间还没有完, 则退出. 否则置当前任务运行计数值位0, 并且若发生时钟中断时
// 正在内核代码中运行则返回, 否则调用执行调度函数
	if ((--current->counter)>0) return;
	current->counter=0;
	if (!cpl) return;  // 内核态程序, 不依赖 counter 值进行调度
	schedule();
}

// 系统调用功能 - 设置报警定时时间值(s)
// 如果 seconds >0, 则设置新定时值, 并返回原定时时刻还剩余的间隔时间. 否则返回0
// 进程数据结构中报警定时值 alarm 的单位时系统滴答(1滴答=10ms), 是系统开机起到设置定时
// 操作时系统滴答值 jiffies 和转换成滴答单位的定时值之和, 即: jiffies + HZ*定时秒值
// 参数给出的是以秒为单位的定时值, 因此本函数的主要操作时进行两种单位的转换. HZ=100,
// 是内核系统运行频率, 定义在 include/sched.h, seconds 是新的定时时间, 单位是秒.
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

// 取当前进程号
int sys_getpid(void)
{
	return current->pid;
}

// 取父进程号
int sys_getppid(void)
{
	return current->father;
}

// 取用户号 uid
int sys_getuid(void)
{
	return current->uid;
}

// 取有效的用户号 euid
int sys_geteuid(void)
{
	return current->euid;
}

int sys_getgid(void)
{
	return current->gid;
}

int sys_getegid(void)
{
	return current->egid;
}

// 系统调用功能: 降低对CPU的使用优先权
int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

// 内核调度程序的初始化子程序
void sched_init(void)
{
	int i;
	struct desc_struct * p;  // 描述符表结构指针

// linux 系统开发之处, 内还不成熟, 内核代码会被经常修改. linus 怕自己无意中修改了这些关键
// 性数据结构, 造成与posix标志不兼容, 因此加入下面这个判断语句, 纯粹是为了提醒自己以及其他
// 修改内核代码的人
	if (sizeof(struct sigaction) != 16)  // sigactioin 存放有关信号状态的结构
		panic("Struct sigaction MUST be 16 bytes");

// 在全局描述符表中设置初始任务(任务0)的任务状态段描述符和局部数据表描述符
// FIRST_TSS_ENTRY 和 FIRST_LDT_ENTRY 的值分别是4和5, 定义在 include/linux/sched.h
// gdt 是一个描述符表数组(include/linux/head.h), 实际上对应程序 head.s 中的全局描述符
// 表基址(_gdt), 因此 gdt+FIRST_TSS_ENTRY 即为 gdt[FIRST_IDT_ENTRY](即gdt[4])
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));

// 清任务数组和描述符表项(注意: i=1开始, 所以初始任务的描述符还在), 描述符项结构定义在
// 文件 include/linux/head.h中
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
// 清除标志寄存器中的 NT位.
// NT 标志用于控制程序的递归调用(Nested task), 当NT置位时, 当前中断任务执行 iret 指令时
// 就会引擎任务切换. NT 指出 tss 中的 back_link 字段是否有效.
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
// 将任务0的tss段选择符加载到任务寄存器tr, 将局部描述符表段选择符加载到局部描述符表寄存器
// ldtr 中. 注意: 是将 GDT 中相应LDT描述符的选择符加载到 ldrt, 以后新任务LDT的加载,是
//CPU根据TSS中的LDT项自动加载
	ltr(0);
	lldt(0);
// 用于初始化 8253 定时器, 通道0, 选择工作方式3, 二进制计数方式. 通道0的输出引脚接在中断
// 控制主芯片的 IRQ0上, 每10ms发一个IRQ0请求. LATCH是初始定时计数值	
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */  // 定时值低字节
	outb(LATCH >> 8 , 0x40);	/* MSB */  // 定时值高字节

// 设置时钟中断处理程序句柄(设置时钟中断门), 修改中断控制器屏蔽码, 允许时钟中断.
// 然后设置系统调用中断门
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	set_system_gate(0x80,&system_call);
}
