/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

# 本代码主要涉及对intel 保留中断 int0-int16的处理, int17-int31留作今后使用
# 以下是一些全局函数名的声明, 其原型在 traps.c 中
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

# 处理无出错号的情况

# int 0 处理被0除出错的情况, 类型: 错误(fault), 错误号: 无
# 在执行 div, idiv 指令时, 若除数是0, cpu 会产生这个异常. 当eax(ax, al)容纳不了一个合法除操作的结果时也会产生这个异常
# _divide_error 实际上是c语言函数 do_divide_error() 编译后生成模块中对应的名称
_divide_error:
	pushl $_do_divide_error  # 首先把将要执行的函数地址入栈
no_error_code:  # 无出错号处理的入口处
	xchgl %eax,(%esp)  # 将 _do_divide_error 的地址与 eax 交换. eax的值入栈
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds  !! 16位段寄存器也要占用4字节
	push %es
	push %fs
	pushl $0		# "error code"  # 将数值0作为出错码入栈  line32
	lea 44(%esp),%edx  # 取堆栈中原调用返回地址处堆栈指针值, 并压入堆栈
	pushl %edx  # line34
	movl $0x10,%edx  # 初始化段寄存器, 加载内核数据段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	call *%eax  # * 表示调用操作数指定地址处的函数, 称为间接调用. 这句的含义是调用引起本次异常的c处理函数, 如do_divide_error
	addl $8,%esp  # 将堆栈指针加8相当于执行两次 pop 操作, 弹出最后入栈的两个c函数参数(弹出line32,line34入栈的两个值),
	# 让堆栈指针重新指向寄存器fs入栈处
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax  # 弹出原来 eax中的内容
	iret

# int 1: debug 调试中断入口点, 处理过程同上, 类型: 错误/陷阱(fault/trap), 错误码: 无
# 当eflags中tf标志置位时而引发的中断, 当发现硬件断点(数据: 陷阱, 代码: 错误); 或开启了指令跟踪陷阱或任务交换陷阱, 或者调试寄存器访问
# 无效(错误), cpu 就产生该异常.
_debug:
	pushl $_do_int3		# _do_debug
	jmp no_error_code

# int 2: 非屏蔽中断调用入口点, 类型: 陷阱; 无错误号
# 这是仅有的被赋予固定中断向量的硬件中断, 每当接受到一个 NMI 信号, CPU 内部就产生中断向量2, 并执行标准中断应答周期, 因此很节省时间
# NMI 通常保留极为重要的硬件事件使用. 当CPU收到一个 NMI 信号并且开始执行其中断处理过程时, 随后所有的硬件中断都将被忽略
_nmi:
	pushl $_do_nmi
	jmp no_error_code

# int3: 断点指令引起中断的入口点, 类型: 陷阱; 无错误号
# 由int 3 指令引发的中断, 与硬件中断无关, 该指令通常由调试器插入被调试程序的代码中, 处理过程同 _debug
_int3:
	pushl $_do_int3
	jmp no_error_code

# int 4: 溢出出错处理中断入口点, 类型: 陷阱, 无错误号
# EFLAGS 中OF标志置位时CPU执行 INTO 指令就好引发该中断, 通常用于编译器跟踪算术计算溢出
_overflow:
	pushl $_do_overflow
	jmp no_error_code
# int 5: 边界检查出错中断入口点, 类型: 错误; 无错误号
# 当操作数在有效范围以外时引发的中断, 当BOUND 指令测试失败就产生该中断. BOUND 指令有3个操作数, 如果第一个不再另外两个之间, 就产生异常5
_bounds:
	pushl $_do_bounds
	jmp no_error_code

# int 6: 无效操作指令出错中断入口, 类型: 错误; 无错误号
# CPU 执行检测到一个无效的操作码而引起的中断
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

# int 9: 协处理器段超出出错中断入口点. 类型: 放弃; 无错误号
# 该异常基本上等同于处理出错保护, 因为浮点指令操作数太大时, 就有机会来加载或保存超出数据段的浮点值
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

# int 15: 其他intel 保留中断入口点
_reserved:
	pushl $_do_reserved
	jmp no_error_code

# int 45: (0x20 + 13) linux设置的数学协处理器硬件中断.
# 当协处理器执行完一个操作时就发出 IRQ13 中断信号, 以通知CPU操作完成 . 80387 在执行计算时, CPU 会等待其操作完成.
# 0xF0 是协处理器端口, 用于清忙锁存器. 通过写该端口, 本中断将消除cpu的busy延续信号, 并重新激活 80387的处理器扩展请求引脚 PEREQ
# 该操作主要是为了确保在继续执行80387的任何命令前, cpu 响应本中断
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20  # 向8259A主控芯片发生EOI(中断结束)
	jmp 1f  # 这两个跳转指令起延时作用
1:	jmp 1f
1:	outb %al,$0xA0  # 再向8259 从中断控制芯片发生 EOI信号
	popl %eax
	jmp _coprocessor_error  # coprocessor_error 原本在本程序中, 现在放到了 system_call.s 中


# 以下中断在调用时cpu会在中断返回地址之后将出错号压入堆栈, 因此返回时也需要将出错号弹出

# int 8: 双出错故障 类型: 放弃; 有错误码
# 通常当 CPPU 在调用前一个异常的处理程序而又检测到一个新的异常时, 这两个异常会被串行的进行处理, 但也会碰到很少的情况, cpu 不能进行
# 这样的串行处理操作, 此时就会引发该中断
_double_fault:
	pushl $_do_double_fault  # c函数地址入栈
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax  # eax原来的值入栈, eax新值是错误码
	xchgl %ebx,(%esp)		# &function <-> %ebx  # ebx 原值入栈, ebx 新值是函数地址
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code  # 出错号入栈
	lea 44(%esp),%eax		# offset  # 程序返回地址处堆栈指针位置入栈
	pushl %eax
	movl $0x10,%eax  # 置内核数据段选择符
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx  # 间接调用, 调用相应的c函数, 其参数已入栈
	addl $8,%esp  # 丢弃入栈的两个用作c函数的参数
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

# int 10: 无效的任务装段(TSS), 类型: 错误, 有出错码
# CPU企图切换到一个进程, 而该进程的TSS无效, 根据TSS中哪一部分引起了异常. 当由于TSS长度超过104字节时, 这个异常在当前任务中产生,
# 因而切换被终止. 其他问题则会导致在切换后的新任务中产生本异常
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

# int 11: 段不存在, 类型: 错误; 有出错码
# 被引用的段不在内存中, 段描述符中标志着段不在内存中
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

# int 12: 堆栈段错误, 类型: 错误, 有出错码
# 指令操作试图超出堆栈段范围, 或者堆栈段不在内存中, 这是异常11和13的特例. 有些操作系统可以利用这个异常来确定什么时候应该为程序分配
# 更多的栈空间
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

# int 13: 一般保护性出错, 类型: 错误; 有出错码
# 表明是不属于任何其他类型的错误. 若一个异常产生时没有对应的处理向量(0-16), 通常就归到此类
_general_protection:
	pushl $_do_general_protection
	jmp error_code

# int7: 设备不存在_device_not_available 在 kernel/system_call.s 
# int 14: 页错误 _page_fault, 在 mm/page.s
# int 16: 协处理器错误 _coprocessor_error, 在 kernel/system_call.s
# 时钟中断 int 0x20 _timer_interrupt, 在 kernel/system_call.s
# 系统调用 int 0x80 _system_call 在 kernel/system_call.s
