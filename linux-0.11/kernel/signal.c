/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

// 函数名前的 volatile 用于告诉编译器gcc 该函数不会返回, 这样可以让gcc产生更好一些的代码
// 更重要的是使用这个关键字可以避免产生某些(未初始化变量的)假警告信息. 等同于现在 gcc 的
// 函数属性说明: void do_exit(int err_code) __attribute__ ((noreturn));
volatile void do_exit(int error_code);
// 获取当前任务信号屏蔽位图(屏蔽码或阻塞码). sgetmask 可分解未 signal-get-mask
int sys_sgetmask()
{
	return current->blocked;
}
// 设置新的信号屏蔽位图, SIGKILL 不能被屏蔽, 返回值是原信号屏蔽位图
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}
// 复制 sigaction 数据到 fs 数据段to处, 即从内核空间复制到用户(任务)数据段中
static inline void save_old(char * from,char * to)
{
	int i;
// 验证to处的内存空间是否足够大, 然后把一个sigaction 结构信息复制到 fs段(用户)空间中
// 宏函数 put_fs_byte() 在 include/asm/segment.h 中
	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}
// 把sigaction 数据从fs数据段from位置复制到to处, 即从用户数据空间复制到内核数据段中
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}
// signal()系统调用. 类似于sigaction(), 为指定的信号安装新的信号句柄. 该信号句柄可以是
// 用户指定的函数,也可以是SIG_DFL,SIG_IGN. 参数 signum 表示信号; handler: 指定的句柄;
// restorer 恢复函数指针, 该函数由libc 提供. 用于在信号处理程序结束后恢复系统调用返回时
// 几个寄存器的原有值以及系统调用的返回值, 就好像系统调用没有执行过信号处理程序而直接返回
// 到用户程序一样. 函数返回原信号句柄
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;
// 先验证信号值是否在有效范围(1-32)内, 并且不得是信号SIGKILL,SIGSTOP. 因为这两个不能被进程捕获
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
// 根据提供的参数组建 sigaction 结构内容. sa_handler 是指定的信号处理句柄(函数).
// sa_mask 是执行信号处理句柄时的信号屏蔽码. sa_flags 是执行时的一些标志组合. 这里设定该信号
// 处理句柄只使用1次后就恢复到默认值, 并允许信号在自己的处理句柄中收到
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;  // 保存恢复处理函数指针.
// 接着取该信号原来的处理句柄, 并设置该信号的 sigaction 结构, 最后返回原信号句柄	
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

// sigaction()系统调用. 改变进程在收到一个信号时的操作. signum 是除了SIGKILL 以外的任何信号
// 如果新操作(action)不为空, 则新操作被安装. 如果oldaction不为空,则原操作被保留到oldaction
// 成功返回0, 否则-1
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
// 在信号的 sigaction 中设置新的操作(动作). 如果oldaction不为空, 则将原操作指针保存到
// oldaction 所指的位置
	tmp = current->sigaction[signum-1];
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
// 如果允许信号在自己的信号句柄中收到, 则令屏蔽码=0, 否则设置屏蔽本信号		
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}
/*系统调用的中断处理程序中真正的信号预处理程序(kernel/system_call.s), 该段代码的主要作用
 是将信号处理句柄插入到用户程序堆栈中, 并在本系统调用结束返回后立刻执行信号句柄程序,然后
 继续执行用户的程序. 该函数尚不能处理进程暂停SIGSTOP等信号.
 函数的参数是进入系统调用处理程序system_call.s开始, 直到调用本函数前逐步压入栈的值.
 这些值包括(system_call.s中的代码行):
 1.CPU执行中断指令压入的用户栈地址 ss,esp, eflags,返回地址cs, eip
 2.在刚进入system_call 时压入栈的ds、es、fs、edx、ecx、ebx
 3.调用sys_call_table 后压入栈中的相应系统调用处理函数的返回值 eax
 4.压入栈中的当前处理的信号值
*/
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;
/*如果信号句柄为SIG_IGN(1,默认忽略句柄)则不对信号进行处理而直接返回;如果句柄为SIG_DFL(0,默认处理)
如果信号是SIGCHLD也直接返回,否则终止进程的执行.
句柄 SIG_IGN被定义为1, SIG_DFL被定义为0; do_exit()的参数是返回码和程序提供的退出状态信息.
可作为 wait(), waitpid()函数的状态信息. wait(),waitpid()利用这些宏可以取得子进程的退出
状态码或子进程终止的原因(信号)
*/
	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));  // 不再返回到这里
	}
/*以下准备对信号句柄的调用设置. 如果该信号句柄只需要使用一次,则将该句柄置空.该信号句柄已经
保存在 sa_handler 指针中. 在系统调用进入内核时, 用户程序返回地址(eip,cs)被保存在内核态栈中
下面这段代码修改内核态堆栈上用户调用系统调用时的代码指针eip为指向信号处理句柄,同时也将sa_restorer
signr、进程屏蔽码(如果SA_NOMASK没置位)、eax、ecx、edx作为参数以及原调用 系统调用的程序返回
指针及eflags压入用户堆栈. 因此本次系统调用中断返回用户程序时会首先指向用户的信号句柄程序,
然后再继续执行用户程序.
*/	
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
/*将内核态栈上用户调用 系统调用下一条代码指令指针eip指向该信号处理句柄. 由于c函数是传值函数
因此给eip赋值时需要使用 *(&eip)的形式. 另外如果允许信号自己的处理句柄收到信号自己,则也需要
将进程的阻塞码压入堆栈.
注意: line104行对普通c函数参数进行修改是不起作用的, 因为当函数返回时堆栈上的参数就会被调用
者丢弃, 这里之所以可以使用这种方式, 是因为该函数是从会被程序中被调用的, 且在函数返回后汇编程序
并没有把调用 do_signal()时的所有参数都丢弃, eip等仍然在堆栈中.
sigaction结构的sa_mask 字段给出了在当前信号句柄(信号描述符)程序执行期间应该被屏蔽的信号集
同时引起本信号句柄执行的信号也会被屏蔽. 若sa_flags 中使用了 SA_NOMASK 标志, 那么引起本信号句柄
执行的信号将不会被屏蔽掉. 如果允许信号自己的处理句柄程序收到信号自己, 则也需要将进程的信号
阻塞码压入堆栈.
*/		
	*(&eip) = sa_handler;  // line104
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
//将原调用程序的用户堆栈指针向下扩展7(或8)个长字(用来存放调用信号句柄的参数等),并检查内存
//使用情况(如过内存超界则分配新页等)
	*(&esp) -= longs;
	verify_area(esp,longs*4);
// 在用户堆栈中从下到上存放 sa_restorer、信号signr、屏蔽码blocked(如果SA_NOMASK置位),
// eax、ecx、edx、eflags和用户程序原代码指针
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
