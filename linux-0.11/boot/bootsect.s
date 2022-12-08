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
	mov	sectors,cx  !!;; 将扇区数存到0x9000:sectors处

	mov	ax,#INITSEG
	mov	es,ax

! Print some inane message

	mov	ah,#0x03		! read cursor pos  !!;; 读光标, 调用号 0x03
	xor	bh,bh
	int	0x10
	
	mov	cx,#24
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1
	mov	ax,#0x1301		! write string, move cursor
	int	0x10  !!;; 显式字符串, 调用号 0x13, al=0x01表示显式模式

! ok, we've written the message, now
! we want to load the system (at 0x10000)

	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it
	call	kill_motor

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.

	seg cs
	mov	ax,root_dev
	cmp	ax,#0
	jne	root_defined
	seg cs
	mov	bx,sectors
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15
	je	root_defined
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

	jmpi	0,SETUPSEG  !!;; 前面的代码是把从硬盘第6个扇区开始往后的240个扇区加载到内存0x10000处, 然后跳转到 0x90200处执行,
	!!;; 这是第二个扇区开始处的内容, 这里的内容就是 setup.s 的内容了.

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
sread:	.word 1+SETUPLEN	! sectors read of current track
head:	.word 0			! current head
track:	.word 0			! current track

read_it:
	mov ax,es
	test ax,#0x0fff  !!;; test 是将两个数做 and 运算
die:	jne die			! es must be at 64kB boundary !!; jne => 使用ZF位, 当ZF=0时跳转; ZF=1时不跳转. jump if not equal => jump if !ZF
	xor bx,bx		! bx is starting address within segment
rp_read:
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet? !!;; 0x1000 + 0x3000 SYSSEG + SYSSIZE
	jb ok1_read
	ret
ok1_read:
	seg cs
	mov ax,sectors  !!;; sectors 中存储了扇区的个数
	sub ax,sread  !!;; 减去已经读入的扇区
	mov cx,ax
	shl cx,#9  !!;; 左移9位
	add cx,bx
	jnc ok2_read
	je ok2_read
	xor ax,ax
	sub ax,bx
	shr ax,#9
ok2_read:
	call read_track
	mov cx,ax
	add ax,sread
	seg cs
	cmp ax,sectors
	jne ok3_read
	mov ax,#1
	sub ax,head
	jne ok4_read
	inc track
ok4_read:
	mov head,ax
	xor ax,ax
ok3_read:
	mov sread,ax
	shl cx,#9
	add bx,cx
	jnc rp_read
	mov ax,es
	add ax,#0x1000
	mov es,ax
	xor bx,bx
	jmp rp_read

read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track
	mov cx,sread
	inc cx
	mov ch,dl
	mov dx,head
	mov dh,dl
	mov dl,#0
	and dx,#0x0100
	mov ah,#2
	int 0x13
	jc bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret
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
kill_motor:
	push dx
	mov dx,#0x3f2
	mov al,#0
	outb
	pop dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55  !!;; 引导扇区的最后两个字节必须是: 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
