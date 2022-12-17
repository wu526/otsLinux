#
# Makefile for the FREAX-kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

AR	=gar
AS	=gas
LD	=gld
LDFLAGS	=-s -x  # 链接程序的参数, -s 输出文件中省略所有符号信息, -x 删除所有局部符号
CC	=gcc
# -Wall 显示所有的警告信息; -O 优化选项; -fstrength-reduce 优化循环执行代码, 排除重复变量; -fomit-frame-pointer 省略保存不必要
#的框架指针; -fcombine-regs 合并寄存器,减少寄存器类的使用; -finline-functions 将所有简单短小的函数代码嵌入调用程序中;
#-mstring-insns linus自己的优化项, 以后不再使用; -nostdinc -I../include 不使用默认路径中的包含文件, 使用-I指定的头文件
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs \
	-finline-functions -mstring-insns -nostdinc -I../../include
CPP	=gcc -E -nostdinc -I../../include  # -E 只运行预处理程序

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<

OBJS  = ll_rw_blk.o floppy.o hd.o ramdisk.o
# 命令中的 rcs 是操作码和修饰标志, 前面可以加上-, 放置次序任意. r是操作码,说明需要执行的操作. r表示要把命令行末列出的目标文件插入
#(替换replacement)归档文件blk_drv.a中. cs 是两个修饰标志, 用于修饰具体操作行为. c表示当归档文件blk_drv.a 不存在时就创建该文件
#s表示写进或更新归档文件中的目标文件索引, 对一个归档文件单独使用命令 ar s 等同于对一个归档文件执行命令 ranlib
blk_drv.a: $(OBJS)
	$(AR) rcs blk_drv.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done
# -M标志告诉预处理程序输出描述每个目标文件相关性的规则, 且这些规则符合make语法. 对于每个源文件, 预处理程序输出一个make规则, 其结果
#形式是相应源程序文件的目标文件名加上其依赖关系: 该源文件中包含的所有头文件列表.
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
floppy.s floppy.o : floppy.c ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/types.h ../../include/linux/mm.h \
  ../../include/signal.h ../../include/linux/kernel.h \
  ../../include/linux/fdreg.h ../../include/asm/system.h \
  ../../include/asm/io.h ../../include/asm/segment.h blk.h 
hd.s hd.o : hd.c ../../include/linux/config.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/linux/hdreg.h \
  ../../include/asm/system.h ../../include/asm/io.h \
  ../../include/asm/segment.h blk.h 
ll_rw_blk.s ll_rw_blk.o : ll_rw_blk.c ../../include/errno.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/asm/system.h blk.h 
