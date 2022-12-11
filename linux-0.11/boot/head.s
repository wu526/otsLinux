/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.text
.globl _idt,_gdt,_pg_dir,_tmp_floppy_area
_pg_dir:  /* 页目录会放在这里 */
/* 这里已经是32位保护模式了, 因此此时的寻址方式发生了变化. 因此这里的 $0x10 并不是把地址 0x10装入各个段寄存器, 它现在其实是全局段
 * 描述符表中的偏移值. 即是一个描述符表项的选择符. 正好指向GDT表中的第2个表项, 即数据段
 */
startup_32:
	movl $0x10,%eax  /* 对于GNU汇编, 每个直接操作数以 $ 开始, 否则表示地址. 每个寄存器以 % 开头 */
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp  /* 表示 _stack_start -> ss:esp 设置系统堆栈, stack_start 定义在 kernel/sched.c 中, 指向
	# user_stack 数组末端的一个长指针. 这里使用的栈姑且称为系统栈. 但在移动到任务0执行(init/main.c)以后该栈就被用作任务0和任务1
	# 共同使用的用户栈了 */
	call setup_idt  /* 重新设置中断描述符 */
	call setup_gdt
	movl $0x10,%eax		# reload all the segment registers  # 因为修改了gdt, 所以需要重新装载所有的段寄存器
	mov %ax,%ds		# after changing gdt. CS was already, cs 以及在 setup_gdt 中重新加载过了
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs

/*
# 由于段描述符中段限长从 setup.s 中的8M改为了本程序中的16M, 因此这里再次对所有段寄存器执行加载操作是必须的. 通过使用bochs跟踪观察
# 如果不对CS再次加载, 那么在执行到 line 35时的段限长还是8M, 这样看来应该重新加载cs, 但由于setup.s 中的内核代码段描述符与本程序中
# 重新设定的代码段描述符除了段限长以外其余完全一样, 8M的段限长在内核初始化阶段不会有任何问题, 而且在以后内核执行过程中段间跳转时会重新
# 加载cs, 因此这里没有加载它并没有让程序出错.
# 针对该问题, 目前内核就在line30行后增加了一条长跳转指令: ljmp $(_KERNEL_CS), $1f, 跳转到line30行来执行确保cs确实重新被加载了.
*/
	lss _stack_start,%esp

/*
# line50~line56 用于测试A20地址线是否已经开启, 采用的方法是向内存地址 0x00000 处写入任意一个数值, 然后看内存地址0x100000处是否
# 也是这个数值, 如果一直相同的话, 就一直比较下去, 即死循环 表示A20没有选通, 结果内核就不能使用1M以上内存
*/
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled, # 1: 是一个局部标号. 此时该标号表示活动位置计数(active location counter)
# 的当前值, 并可以作为指令的操作数. 局部符号用于帮助编译器和编程人员临时使用一些名称. 共有10个局部符号名, 可在整个程序中重复使用, 这些
# 符号使用名称0、1、...、9 来引用. 为了引用先前最近定义的这个符号需要写成 Nb, N是标号数字; 引用一个局部标号的下一个定义要写成 Nf
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 * 486应该将cr0位16置位, 以检查在超级用户模式下的写保护. 此后 verify_area()调用就不需要了. 486 用户通常也会想将NE(#5)置位,
 * 以便对数学协处理器的出错使用 int 16;
 * cr0 的位16 是写保护标志WP(wwrite-protect), 用于禁止超级用户级的程序向一般用户只读页面中进行写操作. 该标志主要用于操作系统在创建
 * 新进程时实现写时复制(copy-on-write)方法.
 */
 # 检查数学协处理芯片是否存在. 方法是修改控制寄存器CR0, 在假设存在协处理器的情况下执行一个协处理指令, 如果出错则说明协处理芯片不
 # 存在, 需要设置 CR0 中的协处理仿真位EM(位2), 并复位协处理器存在标志MP(位1)
 	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	fninit  # fninit, fstsw 是数学协处理器的指令, fninit 向协处理器发出初始化命令, 会把一个协处理器置于一个未受以前操作影响的已知
	# 状态, 设置其控制字为默认值、清除状态字和所有浮点栈式寄存器. 非等待形式的这条指令还会让协处理器终止执行当前正在执行的任何先前的
	# 算术操作. fstsw 指令取协处理器的状态字, 如果系统中存在协处理器的话, 在执行了 fninit 后其状态字低字节肯定=0
	fstsw %ax  # 取协处理器状态字到ax寄存器中
	cmpb $0,%al  # 初始化后状态字应该为0, 否则说明协处理器不存在
	je 1f			/* no coprocessor: have to set bits */  # 如果存在则向前跳到标号1处, 否则改写cr0
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
/*
 * .align 汇编指示符, 含义是指存储边界对齐调整. 2 表示按2^2, 即4字节对齐. 现在GNU as 是直接写出对齐的值, 而非2的次方值了.
 * 使用该指示符的目的是为了提高32位cpu访问内存代码或数据的速度和效率. 后面的2个字节是80287协处理器指令 fsetpm 的机器码. 其作用是
 * 把 80287设置为保护模式, 80387无需该指令, 且将会把该指令看作是空指令
 */	
.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 * 将中断描述符表idt设置成具有256个项, 都指向ignore_int 中断门. 然后加载idt. 真正实用的中断门以后再安装. 当我们在其他地方认为
 * 一切都正常时再开启中断, 该子程序将会被页表覆盖.
 */
/*
 * 中断描述符表中的项也是8字节, 但其格式与全局表中的不同. 被称为门描述符(Gate Descriptor) 它的0-1,6-7字节是偏移量, 2-3字节是选择符
 * 4-5字节是一些标志; 这段代码首先在 edx, eax 中组合设置出8字节默认的中断描述符值, 然后在idt表中每一项中都放置该描述符, 共256项
 * eax 含有描述符低4字节、edx含有高4字节. 内核在随后的初始化过程中会替换安装那些真正实用的中断描述符项.
 */
setup_idt:
	lea ignore_int,%edx  # 将 ignore_int 的有效地址(偏移值)放到 edx 寄存器中
	movl $0x00080000,%eax  # 将钻展符0x8置入eax的高16位中
	movw %dx,%ax		/* selector = 0x0008 = cs */  # 偏移值的低16位值放入 eax 的低16位中, 此时eax含有门描述符低4字节值
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */ # 此时edx含有门描述符高4字节的值

	lea _idt,%edi  # _idt 是中断描述符表的地址
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)  # 将哑中断门描述符存入表中, eax内容放到ds:edi 所指的内存处
	movl %edx,4(%edi)  # edx 内容放到 edi+4 所指内存位置处
	addl $8,%edi  # edi 指向表中下一项
	dec %ecx
	jne rp_sidt
	lidt idt_descr # 加载中断描述符表寄存器值
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
/*
 * 每个页表长为4k, 每个页表项需要4个字节. 因此一个页表共可以存放1024个表项, 如果一个页表项寻址4k空间, 则一个页表就可以寻址4M
 * 页表项格式: 项的0-11位存放一些标志, 如是否在内存中P,读写许可, 普通用户/超级用户, 是否修改过等. 表项的位12-31是页框地址
 * 用于指出一页内存的物理开始地址
 */
.org 0x1000 # 从偏移0x10000处开始是1个页表(偏移0开始处将存放页表目录)
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000  # 定义下面的内存数据从偏移 0x50000处开始
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 * 当DMA不能访问缓冲块时, 下面的 tmp_floppy_area 内存块就可供软盘驱动程序使用. 其地址需要对齐调整. 这样就不会跨越64k边界
 */
_tmp_floppy_area:
	.fill 1024,1,0  # 共保留1024项, 每项1字节, 填充数值0

/*
 * 下面的入栈操作用于为跳转到 init/main.c 中的main函数做准备工作. line139的指令在栈中压入了返回地址, 而189行则压入了main()函数代码
 * 的地址. 当head.s 最后在 line218行执行ret指令时就会弹出main()的地址, 并把控制权转移到 init/main.c 中, 前面3个入栈0值应该分别
 * 表示 envp, argv 指针, argc的值, 但main()没有用到. line139 行的入栈操作是模拟调用 main.c 程序时首先将返回地址入栈的操作,
 * 所以如果main.c 程序真的退出时, 就返回到这里的标号L6处继续执行下去, 即死循环.
 * line140 将main.c 的地址压入栈, 这样在设置分页处理结束后, 执行 ret 返回指令时就好将main.c 的地址弹出堆栈, 并去执行main.c
 * 程序了.
 */
after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to. // line139
	pushl $_main  # line140
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) 下面是默认中断向量句柄 */
int_msg:
	.asciz "Unknown interrupt\n\r"  # 自定字符串未知中断
.align 2  # 按4字节放式对齐内存地址
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds  # 注意: ds,es,fs, gs 等虽然是16位寄存器, 但入栈后仍然会以32位的形式入栈, 即占用4字节
	push %es
	push %fs
	movl $0x10,%eax  # 设置段选择符, 使ds,es,fs指向gdt表中的数据段
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg  # 把调用printk函数的参数指针(地址)入栈, 若符号 int_msg前不加$, 则表示把int_msg 符号处的长字'Unkn'入栈
	call _printk  # 该函数在 kernel/printk.c 中, _printk 是printk编译后模块中的内部表示法
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret  # 中断返回, 把中断调用时压入栈的CPU标志寄存器值也弹出


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 * 机器物理内存中大于1M的内存空间主要被用于主内存区. 主内存区空间由mm模块管理. 涉及到页面映射操作. 内核中所有其他函数就是这里指的
 * 一般(普通)函数, 若要使用主内存区的页面, 就需要使用 get_free_page() 等函数获取. 因为主内存区中内存页面是共享资源, 必须有程序进行
 * 统一管理以避免资源争用和竞争
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */

/*
 * 在物理地址 0x0处开始存放1页页目录表和4页页表. 页目录是系统所有进程公用的, 这里的4页页表则属于内核专用. 它们一一映射线性地址起始16M
 * 空间范围到物理内存上. 对于新的进程, 系统会在主内存区为其申请页面存放页表. 1页=4k字节
 */
.align 2
setup_paging:
	movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */ # 对5页内存清零
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */  # 页目录从0x0地址开始
	cld;rep;stosl  # eax内容存到 es:edi所指内存位置处, 且edi增4
/*
 * 下面4句设置页目录表中的项, 因为内核共有4个页表, 所以只需要设置4项. 页目录项的结构与页表中项的结构一样, 4个字节为1项.
 * 例如 $pg0 + 7 表示 0x00001007 是页目录中第1项; 则第一个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000
 * 第一个页表的属性标志 = 0x00001007 & 0x00000fff = 0x07, 表示该页存在, 用户可读写
 */
	movl $pg0+7,_pg_dir		/* set present bit/user r/w */
	movl $pg1+7,_pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,_pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,_pg_dir+12		/*  --------- " " --------- */
/*
 * 下面6行填写4个页表中所有项的内容, 共有: 4(页表) * 1024(项/页表) = 4096项(0 - 0xfff), 即能映射物理内存 4096 * 4k = 16M
 * 每项的内容是: 当前项所映射的物理内存地址 + 该页的标志(这里均为7)
 * 使用的方法是从最后一个页表的最后一页开始按倒退顺序填写. 一个页表的最后一项在页表中的位置是 1023 * 4 = 4092, 因此最后一页的最后
 * 一项的位置就是 $pg3 + 4092
 */	
	movl $pg3+4092,%edi
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */ # 最后一项对应的物理内存页面的地址是 0xfff000, 加上属性标志7, 即是 0xfff007
	std # 方向位置位, edi 值会递减(4字节)
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax  # 每填好一项, 物理地址值减 0x1000
	jge 1b  # 如果小于0, 则说明全部填写好了
# 设置页目录表基地址寄存器 cr3 的值, 指向页目录表. cr3 中保存的是页目录表的物理地址.
	xorl %eax,%eax		/* pg_dir is at 0x0000 */  # 页目录表在0x0处
	movl %eax,%cr3		/* cr3 - page directory start */
# 设置启动使用分页处理, cr0 的PG标志, 位31	
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */
# 在改变分页处理标志后要求使用转移指令刷新预取指令队列, 这里使用的是返回指令 ret. 该返回指令的另一个作用是将 line140行压入堆栈中的
# main 程序的地址弹出, 并跳转到 init/main.c 中运行. 本程序到此就真正结束了

.align 2
.word 0  # 先空出2字节, 这样 line224 行上的长字是4字节对齐的
idt_descr:
	.word 256*8-1		# idt contains 256 entries  # 共256项, 限长 = 长度 - 1
	.long _idt  # line2244
.align 2
.word 0  # 同理空出2字节
# 这里全局表长度设置为2k, 因为8字节组成一个描述符项, 所以表中可有 256 项
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long _gdt		# magic number, but it works for me :^)

	.align 3
_idt:	.fill 256,8,0		# idt is uninitialized 256项, 每项8字节, 填充0

# 全局表, 前4项分别是空项, 代码段、数据段、系统调用段. 系统调用段并没有派用途, linus当时可能曾项把系统调用代码专门放在这个
# 独立的段中. 后面还预留了 252 项的空间. 用于放置锁创建任务的局部描述符 ldt和对应的任务状态TSS的描述符.
_gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */
	.quad 0x00c0920000000fff	/* 16Mb */
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */
