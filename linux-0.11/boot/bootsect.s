!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
!! SYS_SIZE 是要加载的系统模块长度, 单位是节(1节=16字节). 若以1024字节为1K计算,
!! 0x30000 字节应该等于 192k, 对于当前的版本 空间已经足够了

!! = 或 EQU 用于定义标识符或标号所代表的值, 可称为符号常量. 该常量指明编译链接后system
!! 模块的大小. 该值原来是由 linux/makefile 中的语句动态生成的, 但从 Linux0.11开始就直接
!! 在这里给出了一个最大默认值. 
SYSSIZE = 0x3000
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!! 注意: 目前的内核系统最大长度限制为(8*65536=512k)字节, 即使是在将来也应该没有问题
!! 我想让它保持简单明了. 这样 512k的最大内核长度应该足够了, 尤其这里是没有象minix中
!! 一样包含缓冲区高速缓存
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

!! 伪指令(伪操作符) .globl(.global)用于定义随后的标识符是外部的(全局的), 且即使不使用
!! 也强制引入
!! .text, .data, .bss 分别用于定义当前代码段、数据段、未初始化数据段. 在链接多个目标模块
!! 时, 链接程序(ld86)会根据它们的类别把各自目标模块中的相应段分别组合(合并)在一起.
!! 这里把3个段都定义在同一重叠地址范围中, 因此本程序实际上不分段
.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4				! nr of setup-sectors !! setup程序的扇区数
BOOTSEG  = 0x07c0			! original address of boot-sector
INITSEG  = 0x9000			! we move boot here - out of the way(避开)
SETUPSEG = 0x9020			! setup starts here
SYSSEG   = 0x1000			! system loaded at 0x10000 (65536).
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading

! ROOT_DEV:	0x000 - same type of floppy as boot.  !! 根文件系统设备使用与引导时同样的软驱设备
!		0x301 - first partition on first drive etc !! 根文件系统设备在第一个硬盘的第一个分区上
ROOT_DEV = 0x306
!! 设备号 0x306 指定根文件系统设备是第2个硬盘的第一个分区. 当年linus是在第2个硬盘上安装
!! 了Linux0.11, 所以这里 ROOT_DEV 被设置为 0x306, 在编译这个内核时可以根据自己根文件系统
!! 所在设备位置修改这个设备号. 该设备号时 Linux 系统老式的硬盘设备号命名方式, 硬盘设备号
!! 具体值含义如下:
!! 设备号 = 主设备号 * 256 + 次设备号(即 dev_no=(major << 8) + minor)
!! 主设备号: 1-内存; 2-磁盘; 3-硬盘; 4-ttyx; 5-tty; 6-并行口; 7-非命名管道
!! 参考 img/harddisk02.png
!! 从Linux0.95版本后就已经使用与现在内核相同的命名方法了.

!! 伪指令 entry 让链接程序在生成的执行程序中(a.out)包含指定的标识符或标号
!! line70~line83 作用时将自身(bootsect)从目前位置(0x7c00)移动到0x9000处,然后跳转到
!! 移动后代码的 go 标号处, 即本程序的下一语句处.
entry start
start:
	mov	ax,#BOOTSEG
	mov	ds,ax  !!;; 将ds赋值为 0x07c0; 刚开机时计算处于实模式, 此时寻址方式是 段寄存:offset 的形式.
	mov	ax,#INITSEG
	mov	es,ax  !!;; 将es赋值为 0x9000;
	mov	cx,#256  !!;; 10进制的256, 设置移动计数值
	sub	si,si !!;; 清零si, 设置源地址 ds:si,
	sub	di,di  !!;; 清零di, 设置目标地址: es:di
	rep  !!;; rep 重复指令前缀, 只会影响接下来的一条指令, 重复执行并递减 cx 的值, 直到 cx=0
	movw  !!;; rep movw => 重复执行movw(复制一个字 w: word, word=2byte) 指令, 重复的次数在cx寄存器中, 从 ds:si 复制到 es:di  
	!!;; 即从 0x07c00 复制到 0x90000, 总共复制了 512 byte = 2 * 256

	jmpi	go,INITSEG  !!;; cs=INITSEG, ip=go; 段间跳转(jump intersegment)
	!!;; 因为前面已经将代码移动到了 0x90000处了, 因此需要跳转到 0x9000:go 这个地方执行, 
	!!;; jmpi 段间跳转指令; go 是一个标签, 最终编译成机器码的时候会被翻译成
	!!;; 一个值, 该值就是go这个标签在文件内的偏移地址. 即: go 这个标签在被编译成二进制文件里的内存地址偏移量;
	!!;; 这样就刚好执行到 go 标签所处的这行代码: mov ax, cs

!! 从这里开始, CPU在已移动到 0x90000 位置处的代码中执行. 这段代码设置了几个寄存器, 包括
!! 栈寄存器 ss, sp; 栈指针 sp 只要指向远大于512字节偏移(即地址0x90200)处都可以. 因为
!! 从 0x90200开始要放置setup程序, 此时setup程序大约为4个扇区, 因此 sp 要指向大于(0x200 + 0x200 * 4 + 堆栈大小)处
!! 实际上BIOS把引导扇区加载到 0x7c00处并把执行权交给引导程序时, ss=0x00, sp=0xfffe
go:	mov	ax,cs  !!;; cs = 0x9000
	mov	ds,ax
	mov	es,ax
! put stack at 0x9ff00.
	mov	ss,ax
	mov	sp,#0xFF00		! arbitrary(任意的) value >>512  将堆栈指针sp指向 0x9ff00(即: 0x9000:0xff00)

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.

!! line105~line124 利用BIOS中断 int 0x13 将setup模块从磁盘第2个扇区读到 0x90200处,
!! 共读取4个扇区, 如果读出错则复位驱动器, 并重试, 没有退路.
load_setup:
	mov	dx,#0x0000		! drive 0, head 0
	mov	cx,#0x0002		! sector 2, track 0
	mov	bx,#0x0200		! address = 512, in INITSEG
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors
	int	0x13			! read it !!;; 发起0x13号中断, 0x13是读取磁盘扇区, 读取磁盘时使用的参数是:
	!!;; ah=0x02 读磁盘, al=0x04 读取的扇区数量; ch=0x00 磁道号(柱面号)的低8位,
	!! cl=0x02 开始的扇区编号, 开始扇区(位0~5), 磁道号高2位(位6~7);
	!!;; dh=0x00 磁头号, dl=0x00 驱动器号; 
	!! es:bx(0x9000:0x0200) 存放读入内容的开始位置, 即数据缓冲区
	!! 如果出错则CF标志位置位, ah中是出错码
	!!;; 上面的四条指令用来给 dx,cx,bx,ax赋值. 这4个寄存器是作为这个中断程序的
	!!;; 参数, 这叫做通过寄存器来传参.
	!!;; 从硬盘的第2个扇区开始, 将数据加载到内存 0x90200处, 共加载4个扇区
	
	jnc	ok_load_setup		! ok - continue  !!;; 如果读取错误会设置CF=1; jnc=> jump if not cf
	mov	dx,#0x0000  !!;; 读取错误, 重置ax,dx
	mov	ax,#0x0000		! reset the diskette
	int	0x13
	j	load_setup  !!;;jmp load_setup 不断重试

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
!! 获取磁盘驱动器的参数, 特别是每道的扇区数量;
!! 取磁盘驱动器参数 int 0x13 调用格式和返回信息如下:
!! ah = 0x08 调用号, dl = 驱动器号(如果是硬盘则要置 位7 = 1)
!! 返回信息:
!! 如果出错则CF置位, 且ah=状态码
!! ah=0, al=0, bl=驱动器类型(AT/PS2);
!! ch=最大磁道号的低8位, cl=每磁道最大扇区数(位0~5), 最大磁道号高2位(位6~7)
!! dh=最大磁头数, dl=驱动器数量;  es:di 软驱磁盘参数表
	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters !!;; 读取驱动器的参数信息
	int	0x13
	mov	ch,#0x00
!! seg cs 指令表示下一条语句的操作数在 cs 段寄存器所指的段中. 只影响其下一条指令.
!! 实际上由于本程序代码和数据都被设置处于同一个段中, 即 cs,ds,es 的值相同, 因此本程序
!! 中此处可以不使用该语句
	seg cs
!! 保存每磁道扇区数. 对于软盘来说(dl=0)	其最大磁道号不会超过256, 因此cl的位6、7是0,
!! 前面又已置 ch = 0, 因此此时 cx 中是每磁道扇区数
	mov	sectors,cx  !!;; 将每个磁道包含的扇区数存到0x9000:sectors处

	mov	ax,#INITSEG
	mov	es,ax

! Print some inane message
!! BIOS中断0x10功能号: 0x03 读取光标位置; 输入: bh = 页号; 返回: ch=扫描开始线; cl=扫描结束线;
!! dh=行号(0x00顶端); dl=列号(0x00最左边)
	mov	ah,#0x03		! read cursor pos  !!;; 读光标, 调用号 0x03
	xor	bh,bh
	int	0x10

!! BIOS中断0x10, 功能号ah=0x13 显示字符串. 输入: al=放置光标的方式以及规定属性. 0x01表示使用bl中的属性, 光标停在字符串结尾
!! es:bp 指向要显示的字符串起始位置处. cx=显示的字符串字符数; bh=显示页面号; bl=字符属性; dh=行号; dl=列号
	mov	cx,#24
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1
	mov	ax,#0x1301		! write string, move cursor
	int	0x10  !!;; 显式字符串, 调用号 0x13, al=0x01表示显式模式

! ok, we've written the message, now
! we want to load the system (at 0x10000)

	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it  !! 读磁盘上 system 模块, es为输入参数
	call	kill_motor  !! 关闭驱动器马达, 这样就可以知道驱动器的状态了

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
!! 检查要使用哪个根文件系统设备(根设备), 如果以指定了设备(!=0)就直接使用给定的设备. 否则就需根据BIOS报告的每磁道扇区数
!! 来确定到底使用 /dev/PS0(2,28), 还是 /dev/at0(2,8)
!! 设备文件的含义:
!! Linux中软驱的主设备号是2, 次设备号是: type * 4 + nr, 其中nr为0-3分别对应软驱A、B、C、D type是软驱类型(2->1.2M, 7->1.44M)
!! 因为 7 * 4 + 0 = 28, 所以 /dev/PS0(2,28)指的是1.44M A驱动器, 其设备号是0x21c, 同理 /dev/at0(2,8)指的是1.2M A驱动器,
!! 其设备号是 0x208

!! rot_dev 定义在引导扇区508,509字节处, 指根文件系统所在设备号. 0x0306 指第2个硬盘第一个分区. 这个指需要根据自己根文件系统
!! 所在硬盘和分区进行修改. 例如: 如果根文件系统在第1个硬盘的第1个分区上, 那么该值应该为 0x0301, 即(0x01,0x03), 如果根文件
!! 系统在第2个bochs软盘上, 那么该值应该为 0x021D, 即(0x1D,0x02). 当编译内核时, 可以根据Makefile文件中另行指定该值.
!! 内核映像文件Image的创建程序 tools/build 会使用指定的值来设置根文件系统所在设备号.
	seg cs
	mov	ax,root_dev
	cmp	ax,#0
	jne	root_defined  !! 如果sectors=15说明是 1.2M的驱动器; 如果sectors=18,则说明是1.44M软驱. 因为是可引导的驱动器, 所以是A驱动
	seg cs
	mov	bx,sectors
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15  !! 判断每磁道扇区数是否=15
	je	root_defined  !! 如果等于, 则ax中就是引导驱动器的设备号
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root:  !! 如果都不一样, 则死循环
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax  !! 将检查过的设备号保存到 root_dev 中

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

	jmpi	0,SETUPSEG  !!;; 前面的代码是把从硬盘第6个扇区开始往后的240个扇区加载到内存0x10000处, 然后跳转到 0x90200处执行,
	!!;; 这是第二个扇区开始处的内容, 这里的内容就是 setup.s 的内容了.

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as  !! 确定没有跨越64K的内存边界
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!

!! 伪操作符 .word 定义一个2字节目标. 相当于c中定义的变量和所占内存空间大小; 1+SETUPLEN 表示开始时以读入1个引导扇区和setup
!! 程序所占的扇区数SETUPLEN
sread:	.word 1+SETUPLEN	! sectors read of current track
head:	.word 0			! current head  当前磁头
track:	.word 0			! current track  当前磁道号

!! 读取磁盘上的 system 模块.
!! 首先测试输入的段值. 从磁盘上读入的数据必须存放在位于内存地址64k的边界开始处, 否则进入死循环. 清bx寄存器, 用于表示当前段内存放数据
!! 的开始位置. 233行的test指令以比特位逻辑与两个操作数, 该操作只影响ZF位, AX=0x1000 & 0x0fff = 0 => ZF=1, 即jne条件不成立
read_it:
	mov ax,es
	test ax,#0x0fff  !!;; test 是将两个数做 and 运算
die:	jne die			! es must be at 64kB boundary !!; jne => 使用ZF位, 当ZF=0时跳转; ZF=1时不跳转. jump if not equal => jump if !ZF
	xor bx,bx		! bx is starting address within segment
rp_read:
!! 判断是否以读入全部数据, 比较当前所读段是否就是系统数据末端所处的段(#ENDSEG), 如果不是就跳转到ok1_read 处继续读数据. 否则退出子程序
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet? !!;; 0x1000 + 0x3000 SYSSEG + SYSSIZE
	jb ok1_read
	ret
ok1_read:
!! 计算和验证当前磁道需要读取的扇区数, 放在ax中; 根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置, 计算如果全部读取这些未读
!! 扇区, 所读总字节数是否会超过64K段长度的限制. 若超过, 则根据此次最多能读入的字节数(64K-段内偏移位置), 反算出此次需要读取的扇区数
	seg cs
	mov ax,sectors  !!;; sectors 中存储了每磁道扇区数
	sub ax,sread  !!;; 减去当前磁道已读扇区数
	mov cx,ax  !! cx = ax = 当前磁道未读扇区数
	shl cx,#9  !!;; 左移9位, cx * 512 + 段内当前偏移值(bx)
	add cx,bx !! = 此次读操作后, 段内共读入的字节数
	jnc ok2_read  !! 没有超过64K, 则跳转到 ok2_read 处执行
	je ok2_read
	!! 若加上上次将读磁道上所有未读扇区时会超过64K, 则计算此时最多能读入的字节数(64k-段内读偏移地址), 在转换成需要读取的扇区数
	!! 0减某数就是取该数64K的补值.
	xor ax,ax
	sub ax,bx
	shr ax,#9
ok2_read:
!! 读当前磁道上指定开始扇区(cl)和需读取扇区数(al)的数据到 es:bx, 然后统计当前磁道上已经读取的扇区数并与磁道最大扇区数 sectors 比较
!! 如果小于 sectors 说明当前磁道上还有未读扇区数据. 于是跳转到 ok3_read 继续读.
	call read_track  !! 读当前磁道上指定开始扇区和需读取扇区数的数据
	mov cx,ax  !! cx = 该次操作已读读取的扇区数
	add ax,sread  !! 加上当前磁道上已经读取的扇区数
	seg cs
	cmp ax,sectors  !! 如果当前磁道上还有未读扇区, 跳转到 ok3_read 处
	jne ok3_read
	!! 若该磁道的当前磁头面所有扇区已经读取, 则读取该磁道的下一个磁头面(1号磁头)上的数据
	mov ax,#1
	sub ax,head  !! 判断当前磁头号
	jne ok4_read  !! 如果是磁头0, 则再去读1磁头面上的扇区数据
	inc track  !! 否则去读下一磁道
ok4_read:
	mov head,ax  !! 保存当前磁头号
	xor ax,ax  !! 清当前磁道已读扇区数
ok3_read:
!! 如果当前磁道上还有未读取扇区, 则首先保存当前磁道以读扇区数, 然后调整存放数据处的开始位置, 若小于64K边界值, 则跳转到 rp_read处,继续读数据
	mov sread,ax  !! 保存当前磁道已读扇区数
	shl cx,#9  !! 上次已读扇区数 * 512 字节
	add bx,cx  !! 调整当前段内数据开始位置
	jnc rp_read
	!! 执行到这里说明已经读取64k 数据, 此时调整当前段, 为读下一段做准备
	mov ax,es
	add ax,#0x1000  !! 将段基址调整为下一个64K内存开始处
	mov es,ax
	xor bx,bx  !! 清段内数据开始偏移值
	jmp rp_read  !! 跳转到 rp_read 处, 继续读数据

!! read_track 子程序: 读当前磁道指定开始扇区和需读取扇区数的数据到 es:bx
read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track  !! 取当前磁道号
	mov cx,sread  !! 取当前磁道上已读扇区数
	inc cx  !! cl = 开始读取扇区数
	mov ch,dl  !! dh=当前磁道号
	mov dx,head  !! 取当前磁头号
	mov dh,dl  !! dh = 磁头号
	mov dl,#0  !! dl=驱动器号(0表示当前A驱动器)
	and dx,#0x0100  !! 磁头号不大于1
	mov ah,#2  !! ah=2, 读磁盘扇区功能号
	int 0x13
	jc bad_rt  !! 若出错, 则跳转到 bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret

!! 读磁盘出错, 则执行驱动器复位操作(磁盘中断功能号0), 再跳转到 read_track 处重试
bad_rt:	mov ax,#0
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
!! 0x3f2 是软盘控制器的一个端口, 称为数字输出寄存器(DOR)端口, 是一个8位寄存器, 其为7~4 分别用于控制4个软驱(D~A)的启动和关闭.
!! 位3、2 用于允许、禁止DMA和中断请求以及启动/复位 软盘控制FDC. 位1、0用于选择操作的软驱.
kill_motor:
	push dx
	mov dx,#0x3f2
	mov al,#0  !! 设置al=0, 就是用于选择A驱动器、关闭FDC、禁止DMA和中断请求,关闭马达.
	outb
	pop dx
	ret

sectors:  !! 存放当前启动软盘每磁道的扇区数
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508  !! 表示下面的语句从地址 508 开始
root_dev:
	.word ROOT_DEV  !! 存放根文件系统所在的设备号(init/main.c会使用)
boot_flag:
	.word 0xAA55  !!;; 引导扇区的最后两个字节必须是: 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
