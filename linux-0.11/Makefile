#
# if you want the ram-disk device, define this to be the
# size in blocks.
#
# 如果要使用 RAM盘(RAMDISK)设备的话就定义块的大小. 此处没有定义RAMDISK, 取消注释后gcc编译时会带有
# 选项 -DRAMDISK=512
RAMDISK = #-DRAMDISK=512

AS86	=as86 -0 -a  # 8086汇编编译器和链接器, -0参数表示生成 8086目标程序; -a 生成与gas和gld部分 兼容的代码
LD86	=ld86 -0

AS	=gas
LD	=gld

# GNU链接器gld运行时用到的选项, -s: 输出文件中省略所有的符号信息; -x 删除所有局部符号;
# -M 需要在标准输出设备上打印链接映像(link map), 这是由链接程序产生的一种内存地址映像, 其中列出了程序
# 段装入到内存中的位置信息. 包括:
# 目标文件及符号信息映射到内存中的位置;公共符号如何放置; 链接中包含的所有文件成员及其引用的符号;
LDFLAGS	=-s -x -M
CC	=gcc $(RAMDISK)  # 引用标识符时需要加上$, 类似shell中的语法

# 指定gcc的选项: -Wall 打印所有的警告信息; -O 对代码进行优化; "-f标志"指定与机器无关的编译标志;
# -fstrength-reduce 用于优化循环语句; -fcombine-regs 指明编译器在组合编译阶段把复制一个寄存器到另一个
# 寄存器的指令组合在一起. -fomit-frame-pointer 指明对于无需帧指针(frame pointer)的喊声不要把帧指针
# 保留在寄存器中. 这样在函数中可以避免对帧指针的操作和维护. -mstring-insns 是linus自己移植后新增的
# 指令, 用于 gcc-1.40在复制结构等操作时使用 386 CPU的字符串指令, 可以去掉.
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer \
-fcombine-regs -mstring-insns

# cpp 是gcc的前(预)处理器程序, 预处理器用于进行程序中宏替换处理、条件编译处理以及包含进指定文件的内容
# -nostdinc -Iinclude 表示不要搜索标准头文件目录中的文件, 即不使用系统 /usr/include 目录中的头文件,
# 而是使用 -I指定的目录或是在当前目录中搜索头文件.
CPP	=cpp -nostdinc -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
# ROOT_DEV 指定在创建内核映像(image)文件时所使用的默认根文件系统所在的设备, 这可以时软盘(Floppy)
# /dev/xxx 或者空着, 空着时, build 程序(toos/目录中生成的工具)会使用默认值 /dev/hd6
# /dev/hd6 对应第2个硬盘的第一个分区, 这是Linus开发内核时自己机器上根文件系统所在的分区位置
ROOT_DEV=/dev/hd6

# kernel目录、mm目录、fs目录所产生的目标代码文件, 为了方便引用将它们用 ARCHIVES(归档文件)标识符表示
ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o

# 块和字符设备库文件, .a 表示该文件是归档文件, 即包含有许多可执行二进制代码子程序集合的库文件,
# 通常是用 GNU 的 ar 程序生成. ar 是GNU的二进制文件处理程序, 用于创建、修改以及从归档文件中抽取文件
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH	=kernel/math/math.a  # 数学运算库
LIBS	=lib/lib.a  # lib目录中的文件所编译生成的文件

# make老式的隐式后缀规则. 该行指示make利用下面的命令将所有的 .c 文件编译成 .s 汇编程序.
# 规则命令表示让 gcc采用前面CFLAGS所指定的选项以及仅使用include目录中的头文件在适当地编译后不进行汇编
# 就停止(-S), 从而产生与输入的各个C文件对应的汇编语言形式的代码文件. 默认情况下产生的汇编程序文件是原
# C 文件名去掉 .c 后再加上 .s
# -o 表示其后是输出文件的名称, $*.s ($@)是自动目标变量, $< 代表第一个先决条件, 此处就是符合条件*.c的文件
.c.s:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $<
# 将所有.s 汇编程序文件编译成 .o 目标文件. -c 表示只编译或汇编, 但不进行链接操作
.s.o:
	$(AS) -c -o $*.o $<

# 编译成目标文件但不链接
.c.o:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<

# 创建makefile所知的最顶层的目标. 即Image文件, 这里的Image文件即是引导启动盘映像文件 bootimage,
# 若将其写入软盘就可以使用该软盘引导Linux了.
all:	Image

# 生成Image需要依赖 4 个文件, 规则是: 使用tools目录下的build工具程序将 bootsect, setup, system文件以
# $(ROOT_DEV) 为根文件系统设备组成内核映像文件 Image, sync 是同步命令, 强制让缓冲块数据立即写盘并更新
# 超级块
Image: boot/bootsect boot/setup tools/system tools/build
	tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
	sync

# disk这个目标依赖 Image, /dev/PS0 是指第一个软盘驱动器(设备文件), 在Linux系统下使用 /dev/fd0
disk: Image
	dd bs=8192 if=Image of=/dev/PS0

# 由tools目录下的 build.c 生成执行程序 build
tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

# 会利用上面给出的 .s.o 规则生成 head.o 目标文件
boot/head.o: boot/head.s

tools/system:	boot/head.o init/main.o \
		$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system > System.map

# 数学协处理函数文件
kernel/math/math.a:
	(cd kernel/math; make)

# 生成块设备库文件, 其中含有可重定位目标文件
kernel/blk_drv/blk_drv.a:
	(cd kernel/blk_drv; make)

kernel/chr_drv/chr_drv.a:
	(cd kernel/chr_drv; make)

kernel/kernel.o:
	(cd kernel; make)

mm/mm.o:
	(cd mm; make)

fs/fs.o:
	(cd fs; make)

lib/lib.a:
	(cd lib; make)

# 使用8086汇编和链接器对setup.s 文件进行编译生成 setup 文件. -s 表示要去除目标文件中的符号信息
boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

# 在 bootsect.s 文本程序开始处添加一行有关 system 模块文件长度信息, 在把system模块加载到内存期间用于
# 指明系统模块的长度. 添加该行信息的方法是首先生成只含有 SYSSIZE= system文件实际长度 一行信息的 tmp.s
# 然后将 bootsect.s 文件添加在其后. 其中 (实际长度 + 15)/16用于获得用"节"表示的长度信息, 1节=16字节.
# 这是Linux0.11之前的内核版本获取 system 模块长度并添加到bootsect.s 程序中使用的方法. 从0.11内核开始
# 已经不使用这个方法了, 而是直接在 bootsect.s 程序开始处给出了 system 模块的一个最大默认长度值.
# 因此这个规则已经不起作用了
tmp.s:	boot/bootsect.s tools/system
	(echo -n "SYSSIZE = (";ls -l tools/system | grep system \
		| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
	cat boot/bootsect.s >> tmp.s

# 去除所有编译链接生成的文件.
clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)

# 先执行上面的clean规则, 然后对Linux目录进行压缩, 生成 backup.Z 压缩文件
backup: clean
	(cd .. ; tar cf - linux | compress - > backup.Z)
	sync

# 该目标用于产生各文件之间的依赖关系. 创建这些依赖关系是为了让 make 命令用他们来确定是否需要重建一个
# 目标对象. 如: 当某个头文件被改动过后, make 就能通过生成的依赖关系, 重新编译与该头文件有关的所有*.c
# 文件. 具体方法:
# 使用sed命令对makefile文件(即本文件)进行处理, 输出为删除了 Makefile 文件中 ### Dependencies 行后面
# 的所有行, 并生成一个临时文件tmp_make, 然后对其指定目录下(init/)的每个c文件执行gcc预处理操作.
# -M 告诉预处理器输出描述符每个目标文件相关性的规则, 并且这些规则符合 make 语法. 对于每个源文件预处理器
# 都会输出一个规则, 其结果形式就是相应源程序文件的目标文件名加上其依赖关系, 即该源文件中包含的所有头文件
# 列表. 然后把预处理结果都添加到临时文件 tmp_make 中, 最后将该临时文件复制成新的makefile文件
# $$i 实际上是 $($i), $i是这句前面的 shell 变量 i 的值.
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)

### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 
