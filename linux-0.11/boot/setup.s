!
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!

! NOTE! These had better be the same as in bootsect.s!

INITSEG  = 0x9000	! we move boot here - out of the way !! 原来bootsect所在的段
SYSSEG   = 0x1000	! system loaded at 0x10000 (65536).  !! system 在 0x10000(64k)处
SETUPSEG = 0x9020	! this is the current segment !! setup程序所在的段

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! ok, the read went well so we get current cursor position and save it for
! posterity.

!! 使用BIOS中断读取屏幕当前光标位置(列、行),并保存在内存 0x90000(2字节)
!! 控制台初始化程序会到此处读取该值
!! BIOS 中断 0x10 功能号 ah=0x03, 读光标位置; 输入: bh=页号; 返回 ch=扫描开始线
!! cl=扫描结束线; dh=行号, dl=列号
	mov	ax,#INITSEG	! this is done in bootsect already, but...  !! 重新设置一下ds
	mov	ds,ax
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.

! Get memory size (extended mem, kB)
!! 获取扩展内存大小(KB); 利用BIOS中断 0x15 功能号 ah=0x88 取系统所含扩展内存大小并保存
!! 在内存 0x90002处. 返回: ax=从0x100000(1M)处开始的扩展内存大小(kb), 出错则CF置位,ax=错误码
	mov	ah,#0x88  !!;; 获取扩展内存的调用号, 结果存在ax中, 1M以后的内存叫做扩展内存
	int	0x15
	mov	[2],ax  !! 将扩展内存数值存在 0x90002 处

! Get video-card data:
!! 获取显示卡当前显示模式
!! 调用 BIOS 中断 0x10, 功能号 ah=0x0f
!! 返回: ah=字符列数; al=显示模式; bh=当前显示页
!!0x90004存放当前页, 0x90006存放显示模式; 0x90007 存放字符列数
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width

! check for EGA/VGA and some config parameters
!! 检查显示方式(EGA/VGA)并取参数
!! 使用BIOS中断 0x10, 附加功能选择方式信息. 功能号:ah=0x12, bl=0x10
!! 返回: bh=显示状态; 0x00-彩色模式,I/O端口=0x3dX; 0x01-单色模式, IO端口=0x3bX
!! bl=安装的显示内存: 0x00-64k; 0x01-128k; 0x02-192k; 0x03=256k
!! cx=显示卡特性参数.
	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax  !! 0x90008=??
	mov	[10],bx !! 0x9000A=安装的显卡内存, 0x9000B=显示状态(彩色/单色)
	mov	[12],cx !! 0x9000C = 显卡特性参数

! Get hd0 data
!! 获取第一个硬盘的信息(复制硬盘参数表)
!! 第一个硬盘参数表的首地址是中断向量 0x41 的向量值, 第二个硬盘参数表仅接在第1个表的后面
!! 中断向量0x46的向量值指向第2个硬盘的参数表首地址. 表的长度是16个字节
	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41]  !! 取中断向量 0x41, 即hd0参数表的地址 ds:si
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080  !! 传输的目的地址: 0x9000:0x80 -> es:di
	mov	cx,#0x10  !! 传输16个字节
	rep
	movsb

! Get hd1 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]  !! 取中断向量 0x46的值, 即hd1参数表的地址 ds:si
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090  !! 传输的目的地址 es:di 0x9000:0x90
	mov	cx,#0x10
	rep
	movsb

! Check that there IS a hd1 :-)
!! 检查系统是否有第2个硬盘. 如果没有则把第二个表清零
!! 利用BIOS中断调用 0x13的取磁盘类型功能, 功能号 ah=0x15; 输入: dl=驱动器号(0x8X是硬盘:
!! 0x80指第一个硬盘, 0x81第2个硬盘); 输出: ah=类型码; 00-没有该盘, CF置位;
!! 01-软驱, 没有change-line 支持; 02-软驱或其他可移动设备,有change-line支持; 03-硬盘
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1
	cmp	ah,#3
	je	is_disk1
no_disk1:
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb  !!;; 将ax中的数据填充到 es:di 指向的内存中, 即清零第二个硬盘的数据
is_disk1:

! now we want to move to protected mode ...

	cli			! no interrupts allowed !  !! 关闭中断, 不允许中断

! first we move the system to it's rightful place
!! 首先将system模块移到正确的位置
!! bootsect 引导程序将 system 模块读入到 0x10000(64k)开始的位置, 由于当时假设system
!! 模块最大长度不会超过 0x80000(512K), 即其末端不会超过内存地址0x90000, 所以bootsect
!! 会先把自己移动到0x90000开始的地方, 并把setup加载到它的后面. 下面的代码就是把system
!! 模块移动到0x0处
	mov	ax,#0x0000
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment  !! es:di 是目的地址, 初始值 0x0:0x0
	add	ax,#0x1000
	cmp	ax,#0x9000  !! 已经把最后一段代码移动完了吗?
	jz	end_move  !! 移动完了, 则跳转
	mov	ds,ax		! source segment  !! 源地址: ds:si
	sub	di,di
	sub	si,si
	mov 	cx,#0x8000  !! 每次移动0x8000字(64k字节)
	rep
	movsw
	jmp	do_move  !! 一共移动8次, 共512K

! then we load the segment descriptors
!! 准备加载段描述符
!! 在进入保护模式前, 需要首先设置好需要使用的段描述符表. 这里需要设置全局描述符表和
!! 中断描述符表
!! lidt 用于加载中断描述符表寄存器, 操作数有6字节, 前2字节是描述符表的字节长度值
!! 后4字节是描述符表的32位线性基地址. 中断描述符表中的每一个8字节表项指出发生中断时
!! 需要调用的代码信息, 与中断向量有些相似, 但要包含更多的信息.
!! lgdt 用于加载全局描述符表寄存器, 其操作数格式与 lidt 指令相同. 全局描述符表中的每个
!! 描述符项(8字节)描述了保护模式下数据段和代码段的信息.
end_move:
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax  !! ds指向本程序段(setup)
	lidt	idt_48		! load idt with 0,0
	lgdt	gdt_48		! load gdt with whatever appropriate

! that was painless, now we enable A20
!! 开启A20地址线.
!! 为了能够访问和使用1M以上的物理内存, 需要开启A20地址线. 至于机器是否真的开启了A20地址
!! 线, 还需要在进入保护模式之后(能访问1M以上内存之后)再测试一下. 该工作在head.s 中
	call	empty_8042  !! 测试8042状态寄存器, 等待输入缓冲器空, 只有当输入缓冲器是空时,才可以对其执行写命令
	mov	al,#0xD1		! command write  !! 0xD1命令码表示要写数据到8042的P2端口. P2端口
	!! 的位1用于A20线的选通, 数据要写到0x60口
	out	#0x64,al
	call	empty_8042  !! 等待输入缓冲器空, 看命令是否被接受. 
	mov	al,#0xDF		! A20 on !! 选通A20地址线参数
	out	#0x60,al
	call	empty_8042  !! 若此时输入缓冲器空, 则表示A20线已经选通

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.
!! 重新对中断进行编程. 将它们放在正好处于 intel 保留的硬件中断后面, 即 int 0x20~0x2f
!! 在这里不会引起冲突. 但IBM在原PC机中搞糟了, 以后也没有纠正过来, 所以PC机BIOS把中断
!! 放在了 0x08~0x0f, 这些中断也被用于内部硬件中断. 所以必须重新对 8259中断控制器进行编程

!! 0x00eb 是直接使用机器码表示的两条相对跳转指令, 起延时作用. 0xeb 是直接近跳转指令的操作
!! 码, 带一个字节的相对位移值. 因此跳转范围是:-127~127 cpu 通过把这个相对位移值加到 EIP
!! 寄存器中形成一个新的有效地址. 此时EIP指向下一条被执行指令. 执行时所花费的cpu时钟周期数
!! 是7到10个. 0x00eb 表示跳转值是0的一条指令, 因此还是直接执行下一条指令. 这两条指令共
!! 提供了14到20个CPU时钟周期的延迟时间. 在 as86 中没有表示相应指令的助记符, 因此直接使用
!! 机器码来表示. 每个空指令NOP的时钟周期数是3个
!! 8259芯片主片端口是 0x20, 从片端口是0xA0, 输出值0x11表示初始化命令开始, 是 ICW1命令字
!! 表示边沿触发, 多片8259级联、最后要发送 ICW4 命令字.
	mov	al,#0x11		! initialization sequence
	out	#0x20,al		! send it to 8259A-1  !! 将0x11 发送到8259A主芯片
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2
	out	#0xA0,al		! and to 8259A-2  !! 再发送到8259A从芯片
	.word	0x00eb,0x00eb
	!! Linux系统硬件中断号被设置成从 0x20开始.
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al  !! 送主芯片 ICW2 命令字, 设置开始中断号, 要送奇端口
	.word	0x00eb,0x00eb
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al !! 送从芯片 ICW2 命令字, 从芯片的开始中断号
	.word	0x00eb,0x00eb
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al  !! 送主芯片 ICW3 命令字, 主芯片的IR2连从芯片INT
	.word	0x00eb,0x00eb
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al  !! 送从芯片 ICW3 命令字, 表示从芯片的INT连到主芯片的 IR2 引脚上
	.word	0x00eb,0x00eb
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al  !! 送主片 ICW4 命令字, 8086模式: 普通EOI、非缓冲方式, 需发送指令来复位, 初始化结束, 芯片就绪
	.word	0x00eb,0x00eb
	out	#0xA1,al  !! 送从芯片ICW4 命令字, 同主片
	.word	0x00eb,0x00eb
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0x21,al  !! 屏蔽主芯片所有中断请求
	.word	0x00eb,0x00eb
	out	#0xA1,al  !! 屏蔽从芯片所有中断请求

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x00000, in 32-bit protected mode.

!! 进入32位保护模式, 首先加载机器状态字(lmsw-load machine status word), 
!! CR0的0位置1将导致CPU切换到保护模式, 且运行在特权级0中, 此时段寄存器仍然指向与实地址
!! 模式中相同的线性地址处, 在设置该比特位后, 虽有的一条跳转指令必须实一条段间跳转指令以
!! 用于刷新CPU当前指令队列. 因为CPU在执行一条指令之前就已从内存读取该指令并对其进行了解码
!! 然而在进入保护模式以后那些属于实模式的预先取得的指令信息就变得不再有效. 一条段间跳转
!! 指令就会刷新CPU的当前指令队列, 即丢弃这些无效信息. 另外 intel手册上建议80386或以上
!! cpu使用 mov cr0, ax 切换到保护模式, lmsw仅用于兼容以前的286CPU
	mov	ax,#0x0001	! protected mode (PE) bit
	lmsw	ax		! This is it!
	jmpi	0,8		! jmp offset 0 of segment 8 (cs)

!! 已经将system模块移动到0x0的地方, 所以上句中的偏移地址是0. 段值8此时是保护模式下的段
!! 选择符, 用于选择描述符表和描述符表项以及所要求的特权级. 因此这里会跳转到system中执行

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb,0x00eb
	in	al,#0x64	! 8042 status port  !! 读AT键盘控制器状态寄存器
	test	al,#2		! is input buffer full?  !! 测试位1, 输入缓冲器满?
	jnz	empty_8042	! yes - loop
	ret

gdt:
	!!;; 第0个段描述符, 必须是全0
	.word	0,0,0,0		! dummy

	!!;; 第1个段描述符
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec
	.word	0x00C0		! granularity=4096, 386

	!!;; 第2个段描述符
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write
	.word	0x00C0		! granularity=4096, 386

!! lidt 指令用到的6字节数据
idt_48:
	.word	0			! idt limit=0  !! IDT表的长度
	.word	0,0			! idt base=0L  !! IDT表在线性地址空间中的32位基地址,
	!! CPU要求在进入保护模式前设置IDT表, 因此这里先设置一个长度是0的空表

!! lgdt 指令用到的6字节数据
gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries  !! GDT表的长度
	.word	512+gdt,0x9	! gdt base = 0X9xxxx  !! gdt表的线性基地址
	
.text
endtext:
.data
enddata:
.bss
endbss:
