/*
 *  linux/tools/build.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This file builds a disk-image from three different files:
 *
 * - bootsect: max 510 bytes of 8086 machine code, loads the rest
 * - setup: max 4 sectors of 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 *
 * It does some checking that all files are of the correct type, and
 * just writes the result to stdout, removing headers and padding to
 * the right amount. It also writes some system data to stderr.
 */

/*
 * Changes by tytso to allow root device specification
 */

#include <stdio.h>	/* fprintf */
#include <string.h>
#include <stdlib.h>	/* contains exit */
#include <sys/types.h>	/* unistd.h needs this */
#include <sys/stat.h>
#include <linux/fs.h>
#include <unistd.h>	/* contains read/write */
#include <fcntl.h>

#define MINIX_HEADER 32  //minix二进制目标文件模块头部长度为32字节
#define GCC_HEADER 1024 //GCC头部信息长度为1024字节

#define SYS_SIZE 0x2000 //system文件最长节数(字节数为SYS_SIZE*16=128K)
//默认地把Linux根文件系统所在设备设置为在第2个硬盘的第1个分区上(即设备号为0x0306); 因为把第一个硬盘用作MINIX系统盘, 第二个硬盘用作为Linux
//的根文件系统盘
#define DEFAULT_MAJOR_ROOT 3 //默认根设备主设备号 - 3(硬盘)
#define DEFAULT_MINOR_ROOT 6 //默认根设备次设备号 - 6(第2个硬盘的第1个分区)

/* max nr of sectors of setup: don't change unless you also change 指定setup模块占的最大扇区数: 不要改变该值,除非也改变bootsect等相应文件
 * bootsect etc */
#define SETUP_SECTS 4 //setup最大长度为4个扇区(4 * 512字节)

#define STRINGIFY(x) #x //把x转换成字符串类型,用于出错显示语句中
//显示出错信息,并终止程序
void die(char * str)
{
	fprintf(stderr,"%s\n",str);
	exit(1);
}
//显示程序使用方法, 并退出
void usage(void)
{
	die("Usage: build bootsect setup system [rootdev] [> image]");
}

int main(int argc, char ** argv)
{
	int i,c,id;
	char buf[1024];
	char major_root, minor_root;
	struct stat sb;
//如果程序命令行参数不是4或5个(执行程序本身算作其中1个),则显示程序用法并退出
	if ((argc != 4) && (argc != 5))
		usage();
	if (argc == 5) { //参数是5个,则说明带有根设备名
		if (strcmp(argv[4], "FLOPPY")) { //如果根设备名是软盘FLOPPY,则取该设备文件的状态信息,若出错则显示信息,退出
			if (stat(argv[4], &sb)) {
				perror(argv[4]);
				die("Couldn't stat root device.");
			}
			major_root = MAJOR(sb.st_rdev); //若成功则取该设备名状态结构中的主设备号和次设备号
			minor_root = MINOR(sb.st_rdev);
		} else { //否则让主设备和次设备号=0
			major_root = 0;
			minor_root = 0;
		}
	} else { //若参数只有4个,则让主设备号和次设备后等于系统默认的根设备
		major_root = DEFAULT_MAJOR_ROOT;
		minor_root = DEFAULT_MINOR_ROOT;
	}
//在标准错误终端上显示所选择的根设备主、次设备号	
	fprintf(stderr, "Root device is (%d, %d)\n", major_root, minor_root);
//如果主设备号!=2(软盘)或3(硬盘),也!=0(取系统默认根设备), 则显示出错信息,退出
	if ((major_root != 2) && (major_root != 3) &&
	    (major_root != 0)) {
		fprintf(stderr, "Illegal root device (major = %d)\n",
			major_root);
		die("Bad root device --- major #");
	}
	for (i=0;i<sizeof buf; i++) buf[i]=0; //初始化buf缓冲区,全置0
	if ((id=open(argv[1],O_RDONLY,0))<0) //以只读方式打开参数1指定的文件(bootsect),若出错则显示出错信息,退出
		die("Unable to open 'boot'");
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER) //读取文件中的minix执行头信息,若出错则显示出错信息,退出
		die("Unable to read header of 'boot'");
	if (((long *) buf)[0]!=0x04100301) //0x0301 minix头部a_magic魔数, 0x10 a_flag可执行; 0x04 a_cpu, intel 8086机器码
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[1]!=MINIX_HEADER) //判断头部长度字段a_hdrlen(字节)是否正确(后三字节正好没有用, 是0)
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[3]!=0) //判断数据段长度 a_data字段(long)内容是否为0
		die("Illegal data segment in 'boot'");
	if (((long *) buf)[4]!=0) // 判断堆a_bss 字段(long)内容是否为0
		die("Illegal bss in 'boot'");
	if (((long *) buf)[5] != 0) // 判断执行点 a_entry 字段(long)内容是否为0
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[7] != 0) //判断符号表长度a_sym 的内容是否=0
		die("Illegal symbol table in 'boot'");
	i=read(id,buf,sizeof buf); //读取实际代码数据,应该返回读取字节数为512字节
	fprintf(stderr,"Boot sector %d bytes.\n",i);
	if (i != 512)
		die("Boot block must be exactly 512 bytes");
	if ((*(unsigned short *)(buf+510)) != 0xAA55) //判断boot块0x510处是否有可引导标志0xAA55
		die("Boot block hasn't got boot flag (0xAA55)");
	buf[508] = (char) minor_root; //引导块的508, 509偏移处存放的是根设备号
	buf[509] = (char) major_root;	
	i=write(1,buf,512); //将该boot块512字节的数据写到标准输出stdout, 若写出字节数不对,则显示出错信息,退出
	if (i!=512)
		die("Write call failed");
	close (id); //关闭bootsect模块文件
//显示开始处理setup模块, 以只读方式打开该模块,若出错则显示出错信息,并退出
	if ((id=open(argv[2],O_RDONLY,0))<0)
		die("Unable to open 'setup'");
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER) //读取该文件中的minix执行头信息,若出错则显示出错信息,退出
		die("Unable to read header of 'setup'");
	if (((long *) buf)[0]!=0x04100301) // 0x0301 minix头部 a_magic魔数; 0x10 a_flag可执行, 0x04 a_cpu, intel 8086机器码
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[1]!=MINIX_HEADER) //判断头部长度字段a_hdrlen(字节)是否正确(后3字节正好没有用, 是0)
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[3]!=0) //判断数据段长度a_data字段(long)内容是否=0
		die("Illegal data segment in 'setup'");
	if (((long *) buf)[4]!=0) //判断堆a_bss字段(long)是否=0
		die("Illegal bss in 'setup'");
	if (((long *) buf)[5] != 0) //判断执行点a_entry字段(long)是否=0
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[7] != 0) //判断符号表长字段a_sym 的内容是否=0
		die("Illegal symbol table in 'setup'");
	for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c ) //读取随后的执行代码数据,并写到标准输出stdout
		if (write(1,buf,c)!=c)
			die("Write call failed");
	close (id); //关闭setup模块文件;
	if (i > SETUP_SECTS*512) //若setup模块长度 > 4个扇区,则出错,显示出错信息,退出
		die("Setup exceeds " STRINGIFY(SETUP_SECTS)
			" sectors - rewrite build/boot/setup");
	fprintf(stderr,"Setup is %d bytes.\n",i); //在标准错误stderr显示setup文件的长度值
	for (c=0 ; c<sizeof(buf) ; c++) //将缓冲区buf清零
		buf[c] = '\0';
	while (i<SETUP_SECTS*512) { //若setup长度 <4*512字节,则用0将setup补充为4*512字节
		c = SETUP_SECTS*512-i;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (write(1,buf,c) != c)
			die("Write call failed");
		i += c;
	}
//处理system模块,以只读方式打开该文件;	
	if ((id=open(argv[3],O_RDONLY,0))<0)
		die("Unable to open 'system'");
	if (read(id,buf,GCC_HEADER) != GCC_HEADER) //system模块是GCC格式文件,先读取GCC格式的头部结构信息(linux的执行文件也采用该格式)
		die("Unable to read header of 'system'");
	if (((long *) buf)[5] != 0) //该结构中的执行代码入口点字段a_entry值为0
		die("Non-GCC header of 'system'");
	for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c ) //读取随后的执行代码数据,并写到标准输出stdout
		if (write(1,buf,c)!=c)
			die("Write call failed");
	close(id); //关闭system文件,并向stderr上打印system的字节数
	fprintf(stderr,"System is %d bytes.\n",i);
	if (i > SYS_SIZE*16) //若system代码数据长度超过SYS_SIZE节(或128K字节), 则显示出错信息, 并退出
		die("System is too big");
	return(0);
}
