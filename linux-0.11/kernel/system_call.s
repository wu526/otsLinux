/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 * 注意: 这段代码处理信号(signal)识别, 在每次时钟中断和系统调用之后都会进行识别. 一般中断过程中并不处理信号识别, 因为会给系统造成混乱
 *
 * Stack layout in 'ret_from_system_call':
 * 从系统调用返回(ret_from_system_call)时堆栈上的内容如下:
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */
# linus原注释中的一般中断过程是指 除了系统调用中断(int 0x80)和时钟中断(int 0x20) 以外的其他中断. 这些中断会在内核态或用户态随时
# 发生, 若在这些中断过程中也处理信号识别的话, 就有可能与系统调用中断和时钟中断过程对信号的识别处理过程箱冲突, 违反了内核代码非抢占
# 原则, 因此系统既无必要在这些"其他"中断中处理信号, 也不允许这样做.
SIG_CHLD	= 17  # 定义 SIG_CHLD 信号(子进程停止或结束)

EAX		= 0x00  # 堆栈中各个寄存器的偏移位置
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28  # 当特权级变化时栈会切换, 用户栈指针被保存在内核态栈中
OLDSS		= 0x2C

# 以下这些是任务结构(task_struct) 中变量的偏移值, 参考 include/linux/sched.h 
state	= 0		# these are offsets into the task-struct. 进程状态码
counter	= 4  # 任务运行时间计数(递减)(滴答数), 运行时间片
priority = 8  # 运行优先级， 任务开始运行时, counter = priority, 越大则运行时间越长
signal	= 12  # 信号位图, 每个bit代表一种信号, 信号值=位偏移 + 1
sigaction = 16		# MUST be 16 (=len of sigaction), sigaction 结构长度必须是16字节, 信号执行属性结构数组的偏移值, 对应
                    # 信号将要执行的操作和标志信息
blocked = (33*16)  # 受阻塞信号位图的偏移量

# 以下定义在 sigaction 结构中的偏移量, include/signal.h
# offsets within sigaction
sa_handler = 0  # 信号处理过程的句柄(描述符)
sa_mask = 4  # 信号屏蔽码
sa_flags = 8  # 信号集
sa_restorer = 12  # 恢复函数指针, kernel/signal.c

nr_system_calls = 72  # linux 0.11 中内核中的系统调用总数

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
# 定义入口点
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

# 错误的系统调用号
.align 2  # 内存4字节对齐
bad_sys_call:
	movl $-1,%eax  # eax 置-1, 退出中断
	iret

# 重新执行调度程序入口, 调度程序schedule 在 kernel/sched.c, 当调度程序 schedule() 返回时就从 ret_from_sys_call 处继续执行	
.align 2
reschedule:
	pushl $ret_from_sys_call  # 将ret_from_sys_call 的地址入栈
	jmp _schedule

# int 0x80: linux 系统调用入口点, 调用中断 int 0x80, eax 是调用号
.align 2
_system_call:
	cmpl $nr_system_calls-1,%eax  # 调用号如果超出范围的话就在 eax 中置-1并退出
	ja bad_sys_call
	push %ds  # 保存原段寄存器
	push %es
	push %fs

# 一个系统调用最多带有3个参数, 也可以不带参数. 下面入栈的 ebx, ecx, edx 中放着系统调用相应C函数的调用参数. 这几个寄存器入栈的顺序是
# GNU GCC 规定的, ebx 中可存放第一个参数, ecx 第二个参数, edx 存第3个参数.
# 系统调用语句可参考 include/unistd.h 
	pushl %edx
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space  ds,es 指向内存数据段(全局描述符表中的数据段描述符)
	mov %dx,%ds
	mov %dx,%es
# fs指向局部数据段(局部描述符表中数据段描述符), 即指向执行本次系统调用的用户程序的数据段.
# linux0.11中内核给任务分配的代码和数据内存段是重叠的, 它们的段基址和段限长相同.
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
# 调用地址 = _sys_call_table + eax * 4, _sys_call_table[]是一个指针数组, 定义在 include/linux/sys.h中, 该指针数组中设置了
# 所有 72 个系统调用 C函数的地址.
	call _sys_call_table(,%eax,4)  # 间接调用指定功能c函数
	pushl %eax  # 把系统调用返回值入栈  #line95

# line96~line100 查看当前任务的运行状态, 如果不在就绪状态(state!=0)就执行调度程序, 如果该任务在就绪状态, 但其时间片已经用完,也执行
# 调度程序. 例如当后台进程组中的进程执行控制终端读写操作时, 那么默认条件下该后台进程组所有与进程会收到 sigttin或sigttou 信号, 导致
# 进程组中所有进程处于停止状态, 而当前进程这理解返回.
	movl _current,%eax  # line96  # 取当前任务(进程)数据结构地址到 eax
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule  #line100

# 以下代码执行从系统调用c函数返回后, 对信号进行识别处理. 其他中断服务程序退出时也将跳转到这里进行处理后才退出中断过程
ret_from_sys_call:
# 首先判别当前任务是否是初始任务 task0, 如果是则不必对其进行信号量方面的处理, 直接返回
	movl _current,%eax		# task[0] cannot have signals
	cmpl _task,%eax
	je 3f

# 通过对原调用程序代码选择符的检查来判断调用程序是否是用户任务. 如果不是则直接退出中断. 这是因为任务在内核态执行时不可抢占. 否则对任务
# 进行信号量的识别处理. 这里比较选择符是否为用户代码段的选择符0x000f(rpl=3,局部表, 第一个段(代码段))来判断是否为用户任务. 如果不是
# 则说明是某个中断服务程序跳转到 ret_from_sys_call 行的, 于是跳转退出中断程序. 如果原堆栈段选择符不为 0x17(即原堆栈不在用户段中)
# 也说明本次系统调用的调用者不是用户任务, 则也退出
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f

# line109~line120 用于处理当前任务中的信号, 首先取当前任务结构中的信号位图(32位, 每位代表1种信号), 然后用任务结构中的信号阻塞(屏蔽)
# 码, 阻塞不允许的信号位, 取得数值最小的信号值, 再把原信号位图中该信号对应的位复位(置0), 最后将该信号值作为参数之一调用 do_signal
# do_signal 在 kernel/signal.c 中, 其中参数包括13个入栈的信息
	movl signal(%eax),%ebx #line109
	movl blocked(%eax),%ecx
	notl %ecx  # 每位取反
	andl %ebx,%ecx  # 获得许可的信号位图
	bsfl %ecx,%ecx  # 从低位(位0)开始扫描位图, 看是否有1的位, 若有, 则ecx保留该位的偏移值(即第几位0-31)
	je 3f  # 如果没有信号则向前跳转退出
	btrl %ecx,%ebx  # 复位该信号(ebx 含有原 signal 位图)
	movl %ebx,signal(%eax)  # 重新保存 signal 位图信息, 即 current->signal
	incl %ecx  # 将信号调整为从1开始的数(1-32)
	pushl %ecx  # 信号值入栈作为调用 do_signal 的参数之一
	call _do_signal  # 调用c函数信号处理程序 kernel/signal.c
	popl %eax  # line120  # 弹出入栈的信号值
3:	popl %eax  # eax 含有第line95 入栈的系统调用返回值
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

# int 16 处理器错误中断, 类型: 错误, 无错误码
# 这是一个外部的基于硬件的异常. 当协处理器检测到自己发生错误时, 就通过 ERROR引脚通知CPU, 下面代码用于处理协处理发出的出错信号.
# 并跳转取执行c函数 math_error(), kernel/math/math_emulate.c, 返回后跳转到标号 ret_from_sys_call 处继续执行
.align 2
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax  # ds, es 指向内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax  # FS为指向局部数据段(出错程序的数据段)
	mov %ax,%fs
	pushl $ret_from_sys_call  # 将调用返回的地址入栈
	jmp _math_error  # 执行c函数 math_error()

# int 7: 设备不存在或协处理器不存在. 类型: 错误, 无错误码
# 如果控制寄存器 CR0中EM(模拟)标志置位, 则当cpu执行一个协处理器指令时就会引发该中断, 这样CPU就可以有机会让这个中断处理程序模拟协处理
# 器指令(call _math_emulate); CR0 的交换标志TS是cpu执行任务转换时设置的, TS可以用来确定什么时候协处理器中的内容与CPU正在执行的
# 任务不匹配了. 当CPU在运行一个协处理器转义指令时发现TS置位时, 就引发该中断, 此时就可以保存前一个任务的协处理器内容, 并恢复新任务
# 的协处理器执行状态(line176) kernel/sched.c, 该中断最后将转移到标号 ret_from_sys_call 处执行下去(检测并处理信号)
.align 2
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
# 清CR0中任务已交换标志TS, 并取CR0. 若其中协处理器仿真标志 EM 没有置位, 说明不是 EM 引起的中断, 则恢复任务协处理器状态, 执行C函数
# math_state_restore(), 并在返回时执行 ret_from_sys_call 处的代码
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore  # 执行C函数 math_state_restore(), kernel/sched.c

	# 若EM标志是置位的, 则执行数学仿真程序 math_emulate()
	pushl %ebp
	pushl %esi
	pushl %edi
	call _math_emulate  # 调用c函数 math_emulate(math/math_emulate.c)
	popl %edi
	popl %esi
	popl %ebp
	ret  # 这里的ret将跳转到 ret_from_sys_call

# int32(int 0x20), 时钟中断处理程序, 中断频率被设置为 100Hz(include/linux/sched.h), 定时芯片 8253/8254 是在 kernel/sched.c
# 初始化的, 因此这里 jiffies 每10毫秒+1, 这段代码将 jiffies +1, 发送结束中断指令给8259控制器, 然后用当前特权级作为参数调用c
# 函数do_timer(long cpl), 当调用返回时转去检测并处理信号
.align 2
_timer_interrupt: #line176
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl _jiffies
# 由于初始化中断控制芯片时没有采用自动EOI, 所以这里需要发指令结束该硬件中断
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
# 从堆栈中取出执行系统调用代码的选择符(CS)中的当前特权级并压入栈中, 作为do_timer 的参数, do_timer 函数执行任务切换、记时等工作
# 在kernel/sched.c
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

# 这是sys_execve() 系统调用, 取中断调用程序代码的代码指针作为参数调用c函数do_execve(), 在 fs/exec.c
.align 2
_sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call _do_execve
	addl $4,%esp  # 丢弃调用时压入栈的EIP值
	ret

# sys_fork(), 用于创建子进程, 是 system_call 功能2. 原型在 include/linux/sys.h 中, 首先调用c函数 find_empty_process
# 取得一个进程号pid, 若返回负数说明目前任务数组已满, 然后调佣 copy_process 复制进程
.align 2
_sys_fork:
	call _find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process  # kernel/fork.c
	addl $20,%esp  # 丢弃这里所有压栈内容
1:	ret

# int 46(int 0x2E) 硬盘中断处理程序、响应硬件中断请求 IRQ14.
# 当请求的硬盘操作完成或出错时就发出此中断信号, kernel/blk_drv/hd.c, 首先向8259A中断控制从芯片发送结束硬件中断指令EOI, 然后取变量
# do_hd中的函数指针放入 edx, 并置 do_hd 为NULL, 接着判断edx函数指针是否为空, 如果空, 则给edx赋值指向 unexpected_hd_interrupt,
# 用于显示出错信息, 随后向8259A主芯片送EOI指令, 并调用edx中指针指向的函数, read_intr(), write_intr() 或 unexpected_interrupt
_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
# do_hd 定义为一个函数指针, 将被赋值 read_intr() 或 write_intr(), 放到edx后, 就将 do_hd 指针变量设置为NULL, 然后测试得到的
# 函数指针, 若该指针为空, 则赋予该指针指向C函数 unexpected_hd_interrupt, 以处理未知硬盘中断
1:	xorl %edx,%edx
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20  # 送主8259A EOI
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int38(int 0x26), 软盘驱动中断处理程序, 响应硬件中断请求 IRQ6. 其处理过程与上面对硬盘的处理基本一直.
# 先向 8259A 中断控制主芯片发EOI, 接着判断 eax 函数指针是否为空, 为空则eax赋值为unexpected_interrupt, 用于显示出错信息
# 随后调用 eax 指向的函数: rw_interrupt, seek_interrupt, recal_interrupt,reset_interrupt, unexpected_interrupt
_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
# do_floppy 是一函数指针, 将被赋值实际处理c函数指针, 该指针在被交换放到 eax 后将do_floppy 变量置空. 	
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 39 并行空中断处理程序, 对应硬件中断请求IRQ7, 本版本还未实现, 这里只发生EOI指令
_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
