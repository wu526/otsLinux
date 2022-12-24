#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64 //系统中同时最多任务数(进程数)
#define HZ 100 //定义系统时钟滴答频率(100赫兹, 每个滴答10ms)

#define FIRST_TASK task[0] //任务0比较特殊, 所以特意给它单独定义一个符号
#define LAST_TASK task[NR_TASKS-1] //任务数组中最后一项任务

#include <linux/head.h> //head 头文件, 定义了段描述符的简单结构, 和介个选择符常量
#include <linux/fs.h> //文件系统 头文件, 定义文件表结构(file, buffer_head, m_inode)等
#include <linux/mm.h> //内存管理 头文件, 含有页面大小定义和一些页面释放函数原型
#include <signal.h> // 信号 头文件, 定义信号符号常量,信号结构以及信号操作函数原型

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif
//定义了进程运行可能处的状态
#define TASK_RUNNING		0 //进程正在运行或已准备就绪
#define TASK_INTERRUPTIBLE	1 //进程处于可中断等待状态
#define TASK_UNINTERRUPTIBLE	2 // 进程处于不可中断等待状态, 主要用于IO操作等待
#define TASK_ZOMBIE		3 //进程处于僵尸状态,已停止运行,但父进程还没发信号
#define TASK_STOPPED		4 //进程已停止

#ifndef NULL
#define NULL ((void *) 0) //定义NULL为空指针
#endif
//复制进程的页目录页表.
extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size); //释放页表所指定的内存块及页表本身

extern void sched_init(void); //调度程序的初始化函数
extern void schedule(void); //进程调度函数
extern void trap_init(void);//异常(陷阱)中断处理初始化函数, 设置中断调用门并允许中断请求信号
extern void panic(const char * str); //显示内核出错信息,然后进入死循环
extern int tty_write(unsigned minor,char * buf,int count); // 往tty上写指定长度的字符串

typedef int (*fn_ptr)(); //定义函数指针类型
//数学协处理器使用的结构,主要用于保存进程切换时i387的执行状态信息
struct i387_struct {
	long	cwd; //控制字(control word)
	long	swd; //状态字(status word)
	long	twd; //标记字(tag word)
	long	fip; //协处理器代码指针
	long	fcs; //协处理器代码段寄存器
	long	foo; //内存操作数的偏移位置
	long	fos; //内存操作数的段值
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */ //8个10字节的协处理器累加器
};
//任务状态段数据结构
struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};
//下面是任务(进程)数据结构,或称为进程描述符
struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* -1 unrunnable, 0 runnable, >0 stopped */ //任务的运行状态(-1不可运行, 0可运行就绪 >0已停止)
	long counter; //任务运行时间计数(递减,滴答数),运行时间片
	long priority; //运行优先数,任务开始运行时counter=priority,越大运行越长
	long signal; //信号, 位图. 每个bit位代表一种信号, 信号值=位偏移值+1
	struct sigaction sigaction[32]; //信号执行属性结构, 对应信号将要执行的操作和标志信息
	long blocked;	/* bitmap of masked signals */ //进程信号屏蔽码(对应信号位图)
/* various fields */
	int exit_code; //任务执行停止的退出码,其父进程会取
//start_code代码段地址; end_code代码长度(字节数); end_data 代码长度+数据长度(字节数)； brk:总长度(字节数); start_stack:堆栈段地址
	unsigned long start_code,end_code,end_data,brk,start_stack; 
//pid进程标识号(进程号); father: 父进程号; pgrp: 进程组号; session: 会话号; leader: 会话首领
	long pid,father,pgrp,session,leader;
//uid: 用于标识号(用户id); euid:有效用户id; suid: 保存的用户id; gid:组标识号(组id); egid:有效组id; sgid:保存的组id
	unsigned short uid,euid,suid;
	unsigned short gid,egid,sgid;
	long alarm; //报警定时值(滴答数)
//utime 用户态运行时间(滴答数); stime 系统态运行时间(滴答数)； cutime: 子进程用户态运行时间; cstime: 子进程系统态运行时间; start_time:进程开始运行时刻
	long utime,stime,cutime,cstime,start_time;
	unsigned short used_math; //标志: 是否使用了协处理
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */ //进程使用tty的子设备号, -1表示没有使用
	unsigned short umask; //文件创建属性屏蔽位
	struct m_inode * pwd; // 当前工作目录i节点结构
	struct m_inode * root; //根目录i节点结构
	struct m_inode * executable; // 执行文件i节点结构
	unsigned long close_on_exec; // 执行时关闭文件句柄位图标志
	struct file * filp[NR_OPEN]; //进程使用的文件表结构
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3]; //本任务的局部表描述符, 0空,1代码段; 2数据和堆栈段ds&ss
/* tss for this task */
	struct tss_struct tss; //本进程的任务状态段信息结构
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
//INIT_TASK 用于设置第1个任务表,
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,-1,0,0,0, \
/* uid etc */	0,0,0,0,0,0, \
/* alarm */	0,0,0,0,0,0, \
/* math */	0, \
/* fs info */	-1,0022,NULL,NULL,NULL,0, \
/* filp */	{NULL,}, \
	{ \
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \
		{0x9f,0xc0f200}, \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS]; //任务指针数组
extern struct task_struct *last_task_used_math; //上一个使用过协处理的进程
extern struct task_struct *current; //当前进程结构指针变量
extern long volatile jiffies; //从开机开始算起的滴答数 10ms/滴答
extern long startup_time; //开始时间, 从1970.1.1 0:0:0 开始的秒数

#define CURRENT_TIME (startup_time+jiffies/HZ) //当前时间 秒数

extern void add_timer(long jiffies, void (*fn)(void)); //添加定时器函数(定时时间jiffies滴答数,定时到时调用函数*fn())
extern void sleep_on(struct task_struct ** p); //不可中断的等待睡眠
extern void interruptible_sleep_on(struct task_struct ** p); //可中断的等待睡眠
extern void wake_up(struct task_struct ** p); // 明确唤醒睡眠的进程

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
//在GDT表中寻找第1个TSS的入口,0没用; 1代码段cs; 2数据段ds; 3系统调用syscall; 4任务状态段tss0, 5局部表LTD0,6任务状态段TSS1
#define FIRST_TSS_ENTRY 4 //全局表中第1个任务状态段(TSS)描述符的选择符索引号
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1) //全局表中第1个局部描述符表LDT描述符的选择符索引号
//宏定义: 计算在全局表中第n个任务的TSS段描述符的选择符值(偏移量); 因每个描述符占8字节, 因此FIRST_TSS_ENTRY<<3表示该描述符在GDT表
//中的起始偏移位置. 因为每个任务使用1个TSS和1个LDT描述符,共占用16个字节, 因此需要n<<4来表示对应tss起始位置. 该宏得到的值正好也是该tss的选择符值
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
//宏定义,计算在全局表中第n个任务的LDT段描述符的选择符值(偏移量)
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
//宏定义, 把第n个任务的TSS段选择符加载到任务寄存器TR中
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
//把第n个任务的LDT段选择符加载到局部描述符表寄存器LDTR中
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
//取当前运行任务的任务号(是任务数组中的索引值,与进程号pid不同); 返回n当前任务号, 用于kernel/traps.c
#define str(n) \
__asm__("str %%ax\n\t" \ //将任务寄存器中tss段的选择符复制到ax中
	"subl %2,%%eax\n\t" \ //(eax FIRST_TSS_ENTRY*8) -> eax
	"shrl $4,%%eax" \ //eax/16 -> eax = 当前任务号
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
//将切换当前任务到任务nr,即n. 首先检测任务n不是当前任务, 如果是则什么也不做退出; 如果切换到的任务是最近(上次运行)使用过数学协处理器
//的话,则还需要复位控制寄存器cr0中的ts标志.
//跳转到一个任务的TSS段选择符组成的地址处会造成CPU进行任务切换操作; %0偏移地址(*&__tmp.a); %1存放新tss的选择符; dx 新任务n的tss段选择符
//ecx新任务指针task[n]; 其中临时数据结构__tmp用于组建line177远跳转(far jump)指令的操作数. 该操作数由4字节偏移地址和2字节的段选择符
//组成. 因此__tmp中a的值是32位偏移值, b的低2字节是新TSS段的选择符(高2字节不用). 跳转到TSS段选择符会造成任务切换到该TSS对应的进程.
//对于造成任务切换的长跳转,a值无用. line177上的内存间接跳转指令使用6字节操作数作为跳转目的地的长指针,其格式为: jmp 16位段选择符:32位偏移值
//但在内存中操作数的表示顺序与这里正好相反. 在判断新任务上次执行是否使用过协处理器时,是通过将新任务状态段地址与保存在last_task_used_math
//变量中的使用过协处理器的任务状态段地址进行比较而作出的. 参考 kernel/sched.c 中math_state_restore()
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \ //任务n是当前任务吗?(current==task[n]?)
	"je 1f\n\t" \ //是,什么也不做,退出
	"movw %%dx,%1\n\t" \ //将新任务16位选择符存入 __tmp.b中
	"xchgl %%ecx,_current\n\t" \ //current = task[n]; ecx=被切换出的任务
	"ljmp %0\n\t" \ //执行长跳转至*&_tmp, 造成任务切换  !!;; ljmp 将 cs=__tmp.a, ip=__tmp.b #TODO: 需要确认一下 !!line177
	"cmpl %%ecx,_last_task_used_math\n\t" \ //在任务切换回来后才会继续执行下面的语句. 新任务上次使用过协处理器吗?
	"jne 1f\n\t" \ //没有则跳转,退出
	"clts\n" \ //新任务上次使用过协处理器,清cr0的TS标志
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}
//页面地址对准
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)
//设置位于地址addr处描述符中的各基地址字段(基地址是base). %0 地址addr偏移2; %1 地址addr偏移4; %2 地址addr偏移7; %3 edx 基地址base
#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \ //基址 base低16位(位0~15) [addr+2]
	"rorl $16,%%edx\n\t" \ // edx中基址高16位(位31~16) -> edx
	"movb %%dl,%1\n\t" \ //基址高16位中的低8位(位23~16) -> [addr+4]
	"movb %%dh,%2" \ // 基址高16位中的高8位(位31~24) -> [addr+7]
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:"dx")
//设置位于地址addr处描述符中的段限长字段(段长是Limit); %0 地址addr; %1 地址addr偏移6处, %2 edx 段长值limit
#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \ //段长limit低16位(位15~0) -> [addr]
	"rorl $16,%%edx\n\t" \ //edx中的段长高4位(位19~16)->al
	"movb %1,%%dh\n\t" \ // 取原[addr+6]字节 -> dh, 其中高4位是标志
	"andb $0xf0,%%dh\n\t" \ //清dh的低4位(将存放段长的位19~16)
	"orb %%dh,%%dl\n\t" \ //将原高4位标志和段长的高4位(位19~16)合成1字节
	"movb %%dl,%1" \ //并放会[addr+6]处
	::"m" (*(addr)), \
	  "m" (*((addr)+6)), \
	  "d" (limit) \
	:"dx")
//设置局部描述符表中 ldt描述符的基地址字段
#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 ) //设置局部描述符表中ldt描述符的段长字段
//从地址addr处描述符中取段基地址, 功能与_set_base()正好相反; %0 edx存放基地址(__base); %1 地址addr偏移2; %2 地址addr偏移4
//%3 地址addr偏移7
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \ //取[addr+7]处基址高16位的高8位(位31~24)->dh
	"movb %2,%%dl\n\t" \ //取[addr+4]处基地址高16位的低8位(位23~16)->dl
	"shll $16,%%edx\n\t" \ //基地址高16位移到edx中高16位处
	"movw %1,%%dx" \ //取[addr+2]处基址低16位(位15~0) -> dx
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7))); \
__base;})
//取局部描述符表中ldt所指段描述符中的基地址
#define get_base(ldt) _get_base( ((char *)&(ldt)) )
//取段选择符segment指定的描述符中的段限长值; 指令lsl是load segment limit缩写. 从指定段描述符中取出分散的限长比特位拼成完整的段限长
//值放入指定寄存器中, 所得的段限长是实际字节数-1, 因此这里需要加1后返回. %0 存放段长值(字节数); %1 段选择符 segment
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
