/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
#include <string.h> // 字符串头文件, 主要定义了一些有关内存或字符串操作的嵌入函数

// head 头文件, 定义了段描述符的简单结构, 和几个选择符常量
#include <linux/head.h>
#include <linux/sched.h>  // 调度程序头文件, 定义了任务结构 task_struct、初始任务0的数据, 还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句
#include <linux/kernel.h>  // 内核头文件, 含有一些内核常用函数的原型定义
#include <asm/system.h>  // 系统头文件, 定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include <asm/segment.h>  // 段操作头文件, 定义了有关段寄存器操作的嵌入式汇编函数
#include <asm/io.h>  // 输入/输出头文件, 定义硬件端口输入/输出宏汇编语句

// 以下语句定义了3个嵌入式汇编宏语句函数

// __res 是输出值, 该变量被保存在一个寄存器中, 以便于快速访问和操作. 取段 seg 中地址 addr处的一个字
// 参数: seg 段选择符; addr - 段内指定地址
// 输出: %0 - eax(__res), 输入: %1 - eax(seg); %2 - 内存地址(*addr)
#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

// 取段 seg 中地址 addr 处的一个长字(4字节), 参数: seg 段选择符; addr 段内指定地址
// 输出: %0 eax(__res), 输入: %1 - eax (seg), %2 - 内存地址(*addr)
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

// 取fs段寄存器的值(选择符), 输出: %0 - eax(__res)
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

// 以下定义了一些函数原型
int do_exit(long code);  // 程序退出处理 kernel/exit.c

void page_exception(void);  // 页异常 实际是 page_fault(mm/page.s)

// 以下定义了一些中断处理原型, 用于在函数trap_init()中设置相应中断门描述符.
// 这些函数的代码在 kernel/asm.s 或 system_call.s 中
void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void parallel_interrupt(void);
void irq13(void);

// 用来打印出错中断的名称、出错号、调用程序的EIP、EFLAGS、ESP、fs段寄存器值、段的基址、段的长度、进程号pid、任务号、10字节指令码
// 如果堆栈在用户数据段, 则还打印16字节的堆栈内容
static void die(char * str,long esp_ptr,long nr)
{
	long * esp = (long *) esp_ptr;
	int i;

	// 下行打印语句显示当前调用进程的 CS:EIP, EFLAGS, SS:ESP 的值. 参照图 img/kernel04.png, 这里 esp[0]是图中的 esp0的位置
	// 因此把这语句拆开来看为: esp[1] 是段选择符(cs), esp[0] 是eip; esp[2] 是 eflags; esp[4]是原ss, esp[3]是原esp
	printk("%s: %04x\n\r",str,nr&0xffff);
	printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
		esp[1],esp[0],esp[2],esp[4],esp[3]);
	printk("fs: %04x\n",_fs());
	printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
	if (esp[4] == 0x17) {
		printk("Stack: ");
		for (i=0;i<4;i++)
			printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
		printk("\n");
	}
	str(i); // 取当前运行任务的任务号(include/linux/sched.h), str()是一个宏定义
	printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
	printk("\n\r");
	do_exit(11);		/* play segment exception */
}

// 以下这些以 do_ 开头的函数是 asm.s 中对应中断处理程序调用的c函数
void do_double_fault(long esp, long error_code)
{
	die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
	die("general protection",esp,error_code);
}

void do_divide_error(long esp, long error_code)
{
	die("divide error",esp,error_code);
}

// 参数是进入中断后被顺序压入堆栈的寄存器值.
void do_int3(long * esp, long error_code,
		long fs,long es,long ds,
		long ebp,long esi,long edi,
		long edx,long ecx,long ebx,long eax)
{
	int tr;

	__asm__("str %%ax":"=a" (tr):"0" (0)); // 取任务寄存器值到 tr
	printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
		eax,ebx,ecx,edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
		esi,edi,ebp,(long) esp);
	printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
		ds,es,fs,tr);
	printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
	die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
	die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
	die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
	die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
	die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
	die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
	die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
	die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
	die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
	die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
	if (last_task_used_math != current)
		return;
	die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
	die("reserved (15,17-47) error",esp,error_code);
}

// 下面是异常(陷阱)中断程序初始化子程序. 设置它们的中断调用门(中断向量). set_trap_gate(), set_system_gate() 都使用了中断
// 描述符表 IDT中的陷阱门(trap gate), 它们之间的主要区别在于前者设置的特权级为0, 后者是3. 因此断点陷阱中断 int3, 溢出中断
// overflow 和边界出错中断 bounds 可以由任何程序产生
// set_trap_gate, set_system_gate 是嵌入式汇编宏程序(include/asm/system.h)
void trap_init(void)
{
	int i;

	set_trap_gate(0,&divide_error);  // 设置除法操作除出错的中断向量值
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */  // int3-5可以被所有程序执行
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);

	// 把int17-47的陷阱门先均设置为 reserved, 以后各硬件初始化时会重新设置自己的陷阱门
	for (i=17;i<48;i++)
		set_trap_gate(i,&reserved);

	// 设置协处理器中断 0x2d(45)陷阱门描述符, 并允许其产生中断请求, 设置并行口中断描述符
	set_trap_gate(45,&irq13);
	outb_p(inb_p(0x21)&0xfb,0x21);  // 允许8259A 主芯片的IRQ2中断请求
	outb(inb_p(0xA1)&0xdf,0xA1);  // 允许8259A 从芯片的IRQ13 中断请求
	set_trap_gate(39,&parallel_interrupt);  // 设置并行口 1的中断0x27 陷阱门描述符
}
