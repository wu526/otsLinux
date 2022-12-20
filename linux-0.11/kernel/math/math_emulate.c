/*
 * linux/kernel/math/math_emulate.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This directory should contain the math-emulation code.
 * Currently only results in a signal.
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
//协处理器仿真函数. 中断处理程序调用的c函数.
void math_emulate(long edi, long esi, long ebp, long sys_call_ret,
	long eax,long ebx,long ecx,long edx,
	unsigned short fs,unsigned short es,unsigned short ds,
	unsigned long eip,unsigned short cs,unsigned long eflags,
	unsigned short ss, unsigned long esp)
{
	unsigned char first, second;

/* 0x0007 means user code space */ //0x0007表示用户代码空间.
//0x000F表示在局部描述符表中描述符索引值=1, 即代码空间. 如果段寄存器cs!=0x000F,则表示cs
//一定是内核代码选择符,是在内核代码空间,则出错.显示此时的cs:eip值,并显示信息,然后进入死机
	if (cs != 0x000F) {
		printk("math_emulate: %04x:%08x\n\r",cs,eip);
		panic("Math emulation needed in kernel");
	}
//取用户进程进入中断时当前代码指针cs:eip处的两个字节机器码first,second, 是这个指令引发了
//本中断,然后显示进程的cs,eip值和这两个字节,并给进程设置浮点异常信号SIGFPE
	first = get_fs_byte((char *)((*&eip)++));
	second = get_fs_byte((char *)((*&eip)++));
	printk("%04x:%08x %02x %02x\n\r",cs,eip-2,first,second);
	current->signal |= 1<<(SIGFPE-1);
}
//协处理器出错处理函数;中断处理程序调用的c函数.
void math_error(void)
{
	__asm__("fnclex");//协处理器指令,以非等待形式清除所有异常标志、忙标志和状态字位7
	if (last_task_used_math)//如果上个任务使用过协处理器,则向上个任务发送协处理器异常信号
		last_task_used_math->signal |= 1<<(SIGFPE-1);
}
