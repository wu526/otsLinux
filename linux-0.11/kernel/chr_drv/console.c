/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 * 有时短暂地禁止和允许中断(当输出一个字(word)到视频(IO)),但即使对于键盘中断也是可以工作的
 * 因为使用陷阱门,所以知道在处理一个键盘中断过程时中断被禁止.
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time: setup程序在引导系统时设置的参数
 */

#define ORIG_X			(*(unsigned char *)0x90000)//初始光标列号
#define ORIG_Y			(*(unsigned char *)0x90001)  // 初始光标行号
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004)  //显示页面
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff)  // 显示模式
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8) //字符列数
#define ORIG_VIDEO_LINES	(25)  //显示行数
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a)  // 显示内存大小和色彩模式
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c)  // 显示卡特性参数
//定义显示器单色/彩色显示模式类型符号常数.
#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/  // 单色文本
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/  // cga 显示器
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/  // EGA/VGA单色
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/ //EGA/VGA彩色

#define NPAR 16  // 转义字符序列中最大参数个数

extern void keyboard_interrupt(void);  // 键盘中断处理程序

static unsigned char	video_type;		/* Type of display being used	*/ //使用的显示类型
static unsigned long	video_num_columns;	/* Number of text columns	*/  // 屏幕文本列数
static unsigned long	video_size_row;		/* Bytes per row		*/  // 屏幕每行使用的字节数
static unsigned long	video_num_lines;	/* Number of test lines		*/  // 屏幕文本行数
static unsigned char	video_page;		/* Initial video page		*/  // 初始显示页面
static unsigned long	video_mem_start;	/* Start of video RAM		*/  // 显示内存起始地址
static unsigned long	video_mem_end;		/* End of video RAM (sort of)	*/  // 显示内存结束(末端)地址
static unsigned short	video_port_reg;		/* Video register select port	*/  //显示控制索引寄存器端口
static unsigned short	video_port_val;		/* Video register value port	*/ //显示控制数据寄存器端口
static unsigned short	video_erase_char;	/* Char+Attrib to erase with	*/ //擦除字符属性及字符(0x0720)
//下面的变量用于屏幕卷屏操作. origin表示移动的虚拟窗口左上角原点内存地址
static unsigned long	origin;		/* Used for EGA/VGA fast scroll	*/  //用于EGA/VGA快速滚屏, 滚屏起始内存地址
static unsigned long	scr_end;	/* Used for EGA/VGA fast scroll	*/ //用于EGA/VGA快速滚屏,滚屏末端内存地址
static unsigned long	pos;//当前光标对于的显示内存位置
static unsigned long	x,y;//当前光标位置
static unsigned long top, bottom; // 滚动时顶行行号;底行行号
//state用于标明处理ESC转义序列时的当前步骤, par[]用于存放ESC序列的中间处理参数
static unsigned long	state=0;//ANSI 转义字符序列处理状态
static unsigned long	npar,par[NPAR];  //ANSI转义字符序列参数个数和参数数组
static unsigned long	ques=0;  //收到问号字符标志
static unsigned char	attr=0x07; // 字符属性(黑底白字)

static void sysbeep(void); //系统蜂鸣函数

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).终端回应ESC-Z或csi0c请求的应答(=vt100响应)
 */
// csi-控制序列引导码(control sequence introducer), 主机通过发送不带参数或参数是0的设备属性
//(DA)控制序列(ESC [c, ESC[0c) 要求终端应答一个设备属性控制序列(ESC Z的作用与此相同),
// 终端则发送以下序列来响应主机. 该序列(ESC [?1;2c)表示终端是高级视频终端
#define RESPONSE "\033[?1;2c"

/* NOTE! gotoxy thinks x==video_num_columns is ok */ //gotoxy()认为 x==video_num_columns时时正确的
// 跟踪光标当前位置 参数: new_x 光标所在列号; new_y 光标所在行号. 更新当前光标位置变量x,y 并修正光标在显存中的对应位置pos
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
//检查参数的有效性,如果给定的光标列号超出显示器列数,或光标行号不低于显示的最大行数,则退出.
//否则就更新当前光标变量和新光标位置对应在显存中位置pos
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	x=new_x;
	y=new_y;
	pos=origin + y*video_size_row + (x<<1);
}
//设置滚屏起始显存地址
static inline void set_origin(void)
{
//先向显示寄存器选择端口 video_port_reg 输出12,即选择显示控制数据寄存器r12,然后写入卷屏起始地址高字节
//向右移动9位,表示向右移动8位再除以2(屏幕上1个字符用2个字节表示). 再选择显示控制数据寄存器r13,然后写入
//卷屏起始地址低字节,向右移动1位,同样代表屏幕上1个字符用2个字节表示. 输出值是相对于默认显示内存
//起始位置video_mem_start 操作的,如:彩色模式,video_mem_start=0xb800(物理内存地址)
	cli();
	outb_p(12, video_port_reg); //选择数据寄存器r12, 输出卷屏起始位置高字节
	outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);
	outb_p(13, video_port_reg);  //选择数据寄存器r13, 输出全屏起始位置低字节
	outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
	sti();
}
// 向上卷动一行. 将屏幕滚动区域中内容向下移动一行,并再区域顶出现的新行上添加空格字符.滚屏区域必须起码是2行或2行以上.
static void scrup(void)
{
//先判断显卡类型. 对于EGA/VGA卡,可以指定屏内行范围(区域)进行滚屏操作,MDA单色显示卡只能进行
//整屏滚屏操作. 该函数对EGA和MDA显示类型进行分别处理.EGA显示类型还分为整屏窗口移动和区域内窗口移动
//这里首先处理显卡是EGA/VGA显示类型的情况
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
//如果移动起始行top=0,移动最低行bottom=video_num_lines=25,则表示整屏窗口向下移动.于是把整个屏幕窗口
//左上角对应的起始内存位置origin调整为向下移一行对应的内存位置,同时调整当前光标对应的内存位置
//以及屏幕末行末端字符指针scr_end的位置.最后把新屏幕滚动窗口内存起始位置值origin写入显示控制器中
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
//如果屏幕窗口末端所对应的显示内存指针scr_end超出了实际显示内存末端,则将屏幕内容除第一行以外
//所有行对应的内存数据移动到显示内存的起始位置video_mem_start处,并再整屏窗口向下移动出现的新
//行上填入空格字符,然后根据屏幕内存数据移动后的情况,重新调整当前屏幕对应内存的起始指针、光标
//位置指针和屏幕末端对应内存指针scr_end. 该段汇编首先将(屏幕字符行数-1)行对应的内存数据移动
//到显示内存起始位置video_mem_start处,然后在随后的内存位置处添加一行空格(擦除)字符数据.
//%0 eax 擦除字符+属性; %1 ecx 屏幕字符行数-1 所对应的字符数/2, 以长字移动;
//%2 edi 显示内存起始位置 video_mem_start; %3 esi 屏幕窗口内存起始位置origin
//移动方向: edi->esi, 移动 ecx个长字
			if (scr_end > video_mem_end) {
				__asm__("cld\n\t"
					"rep\n\t" //重复操作,将当前屏幕内存数据移动到显示内存起始处
					"movsl\n\t"
					"movl _video_num_columns,%1\n\t"
					"rep\n\t"  //在新行上填入空格字符
					"stosw"
					::"a" (video_erase_char),
					"c" ((video_num_lines-1)*video_num_columns>>1),
					"D" (video_mem_start),
					"S" (origin)
					:"cx","di","si");
				scr_end -= origin-video_mem_start;
				pos -= origin-video_mem_start;
				origin = video_mem_start;
			} else {
//调整后的屏幕末端对应的内存指针scr_end没有超出显示内存的末端 video_mem_end,则只需在新行
//上填入擦除字符(空格字符) %0 eax(擦除字符+属性); %1 ecx(屏幕字符行数); %2 edi 最后1行开始处对应内存位置
				__asm__("cld\n\t"
					"rep\n\t"  // 重复操作,在新出现行上填入擦除字符(空格字符)
					"stosw"
					::"a" (video_erase_char),
					"c" (video_num_columns),
					"D" (scr_end-video_size_row)
					:"cx","di");
			}
			set_origin();//把新屏幕滚动窗口内存起始位置值origiin写入显示控制器中
		} else {
//不是整屏移动,从指定行top开始到bottom区域中的所有行向上移动1行,指定行top被删除,此时直接
//将屏幕从指定行top到屏幕末端所有行对应的显示内存数据向上移动1行,并在最下面新出现的行上填入
//擦除字符. %0 eax 擦除字符+属性; %1 ecx(top行下1行开始到bottom行所对应的内存长字数);
//%2 edi top行所处的内存位置; %e esi top+1行所处的内存位置
			__asm__("cld\n\t"
				"rep\n\t"//循环操作,将top+1到bottom行所对应的内存块移到top行开始处
				"movsl\n\t"
				"movl _video_num_columns,%%ecx\n\t"
				"rep\n\t" //新行填入擦除字符
				"stosw"
				::"a" (video_erase_char),
				"c" ((bottom-top-1)*video_num_columns>>1),
				"D" (origin+video_size_row*top),
				"S" (origin+video_size_row*(top+1))
				:"cx","di","si");
		}
	}
	else		/* Not EGA/VGA */
	{
//显示类型不是EGJA(是MDA),则执行下面移动操作. 因为MDA显示卡只能整屏滚动,且会自动调整超出
//显示范围的情况,即会自动翻卷指针,所以这里不对屏幕内容对应内存超出显示内存的情况单独处理
//处理方法于EGA非整屏移动情况一样
		__asm__("cld\n\t"
			"rep\n\t"
			"movsl\n\t"
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*top),
			"S" (origin+video_size_row*(top+1))
			:"cx","di","si");
	}
}
//向下卷动一行; 将屏幕滚动窗口向上移动一行,相应屏幕滚动区域内容向下移动1行.并在移动开始行
//的上方出现一新行,处理方法与scrup()类似.只是为了在移动显示内存数据时不会出现数据覆盖的
//问题, 复制操作以逆向进行, 即先从屏幕倒数第2行的最后一个字符开始复制到最后一行,再将倒数第
//3行复制到倒数第2行等. 因为此时对EGA/VGA显示类型和MDA类型的处理过程完全一样,所以该函数
//实际上没有必要写两段相同的代码. 即这里if和else语句块中的操作完全一样.
static void scrdown(void)
{
//本函数针对EGA/VGA 和MDA 分别进行操作. 如果显示类型是EGA,则执行下列操作. 这里if、else中
//的操作完全一样,以后的内核就合二为一了.	
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
//%0 eax 擦除字符+属性; %1 ecx(top行到bottom-1行的行数所对应的内存长字数);
//%2 edi 窗口右下角最后一个长字位置; %3 esi 窗口倒数第2行最后一个长字位置
//移动方向 esi -> edi, 移动ecx个长字.
		__asm__("std\n\t" // 置方向位
			"rep\n\t"  // 重复操作,向下移动从top行到bottom-1行对应的内存数据
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */ //edi已减4,反向填擦除字符
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"  //将擦除字符填入上方新行中
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
	else		/* Not EGA/VGA */
	{
//不是EGA显示类型,则执行以下操作		
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
}
//光标位置下移一行(lf-line feed换行); 如果光标没有在最后一行上,则直接修改光标当前行变量
//y++,并调整光标对应显示内存位置pos(加上一行字符所对应的内存长度).否则就需要将屏幕窗口内容
//上移一行. 函数名lf 是指处理控制字符LF
static void lf(void)
{
	if (y+1<bottom) {
		y++;
		pos += video_size_row;  //加上屏幕一行占用内存的字节数
		return;
	}
	scrup();//将屏幕窗口内容上移一行
}
//光标在同列上移一行; 如果光标不在屏幕第一行,则直接修改光标当前行变量y--,并调整光标对应显存
//位置pos,减去屏幕上一行字符所对应的内存长度字节数. 否则需要将屏幕窗口内容下移一行.
//函数名称ri(reverse index 反向索引)是指控制字符RI或转义序列 ESC M
static void ri(void)
{
	if (y>top) {
		y--;
		pos -= video_size_row;  // 减去屏幕一行占用内存的字节数
		return;
	}
	scrdown();//屏幕窗口内容下移一行
}
//光标回到第1列(0列); 调整光标对应内存位置pos. 光标所在列号*2即是0列到光标所在列对应的内存
//字节长度. 函数名称cr(carriage return 回车)指明处理的控制字符是回车字符.
static void cr(void)
{
	pos -= x<<1; //减去0列到光标处所占用的内存字节数
	x=0;
}
//擦除光标前一字符(用空格代替)(del - delete 删除); 如果光标没有处在0列,则将光标对应内存位置
//pos后退2字节(对应屏幕上一个字符),然后将当前光标变量列值-1, 并将光标所在位置处字符擦除.
static void del(void)
{
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}
// 删除屏幕上与光标位置相关的部分. ANSI控制序列: ESC [PS J PS=0 -删除光标处到屏幕底端;
//1删除屏幕开始到光标处; 2整屏删除; 本函数根据指定的控制序列具体参数值,执行与光标位置相关
//的删除操作,且在擦除字符或行时 光标位置不变. csi_J(CSI-control sequence introducer,即
//控制序列引导码), 指明对控制序列 csi ps j 进行处理, par: 对应控制序列中 ps 的值.
static void csi_J(int par)
{
	long count __asm__("cx");  // 设为寄存器变量
	long start __asm__("di");
//首先根据3种情况分别设置需要删除的字符数和删除开始的显示内存位置
	switch (par) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;  // 擦除光标到屏幕底端所有字符
			start = pos;
			break;
		case 1:	/* erase from start to cursor */
			count = (pos-origin)>>1;  // 删除从屏幕开始到光标处的字符
			start = origin;
			break;
		case 2: /* erase whole display */ //删除整个屏幕上的所有字符
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
//使用擦除字符填写被删除字符的地方. %0 ecx 删除的字符数count; %1 edi 删除操作开始地址
//%2 eax 填入的擦除字符	
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}
//删除一行上与光标位置相关的部分. ansi转义字符序列 ESC[Ps K, ps=0删除到行尾; 1从开始删除;
//2整行删除; 本函数根据参数擦除光标所在行的部分或所有字符. 擦除操作从屏幕上移走字符但不影响
//其他字符. 擦除的字符被丢弃,在擦除字符或行时光标位置不变. 参数: par 对应控制序列中Ps值
static void csi_K(int par)
{
	long count __asm__("cx"); // 设置寄存器变量
	long start __asm__("di");
//根据三种情况分别设置需要删除的字符数和删除开始的显示内存位置.
	switch (par) {
		case 0:	/* erase from cursor to end of line */
			if (x>=video_num_columns)//删除光标到行尾所有字符
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - (x<<1); //删除从行开始到光标处
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: /* erase whole line */
			start = pos - (x<<1); //将整行字符全部删除
			count = video_num_columns;
			break;
		default:
			return;
	}
//使用擦除字符填写删除字符的地方; %0 ecx 删除字符数count; %1 edi 删除操作开始地址
//%2 eax 填入的擦除字符	
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}
//设置显示字符属性; ESC[Ps m  Ps=0默认属性; 1加粗; 4加下划线; 7反显; 27正显; 该控制序列
//根据参数设置字符显示属性, 以后所有发送到终端的字符都将使用这里指定的属性. 直到再次执行本
//控制序列重新设置字符显示的属性. 对于单色和彩色显示卡, 设置的属性有区别, 这里仅做简化处理
void csi_m(void)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:attr=0x07;break;
			case 1:attr=0x0f;break;
			case 4:attr=0x0f;break;
			case 7:attr=0x70;break;
			case 27:attr=0x07;break;
		}
}
//设置显示光标; 根据光标对应显示内存位置pos, 设置显示控制器光标的显示位置.
static inline void set_cursor(void)
{
//首先使用索引寄存器端口选择显示控制数据寄存器r14(光标当前显示位置高字节),然后写入光标
//当前位置高字节(向右移动9位表示高字节移到低字节再除以2), 是相对于默认显示内存操作的.
//再使用索引寄存器选择r15,将光标当前位置低字节写入其中.
	cli();
	outb_p(14, video_port_reg);  // 选择数据寄存器r14
	outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
	outb_p(15, video_port_reg);//选择数据寄存器r15
	outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
	sti();
}
//发送对VT100的响应序列; 即为响应主机请求终端向主机发送设备属性(DA). 主机通过发送不带参数或
//参数是0的DA控制序列 ESC[0c 或 ESC[Z 要求终端发送一个设备属性(DA)控制序列,终端则发送定义
//号的应答序列(即ESC[?1;2c)来响应主机的序列,该序列告诉主机本终端是具有高级视频功能的VT100
//兼容终端,处理过程是将应答序列放入读缓冲队列中,并使用copy_to_cooked()处理后放入辅助队列中
static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE; //前面定义的应答序列, ESC[1?;2c

	cli();
	while (*p) {
		PUTCH(*p,tty->read_q);//将应答序列读入队列,逐字放入
		p++;
	}
	sti();
	copy_to_cooked(tty);//转换成规范模式,放入辅助队列中
}
//在光标处插入一空格字符. 把光标开始处的所有字符右移一格,将擦除字符插入在光标所在处.
static void insert_char(void)
{
	int i=x;
	unsigned short tmp, old = video_erase_char;  //擦除字符(加属性)
	unsigned short * p = (unsigned short *) pos; //光标对应内存位置

	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}
//在光标处插入一行; 将屏幕窗口从光标所在行到窗口底的内容向下卷动一行. 光标将处在新的空行上
static void insert_line(void)
{
	int oldtop,oldbottom;
//保存屏幕窗口卷动开始行top和最后行bottom值,然后从光标所在行让屏幕内容向下滚动一行.最后
//恢复屏幕窗口卷动开始行top和最后行bottom的原来值
	oldtop=top;
	oldbottom=bottom;
	top=y;//设置屏幕卷动开始行和结束行
	bottom = video_num_lines;
	scrdown(); //从光标开始处,屏幕内容向下滚动一行
	top=oldtop;
	bottom=oldbottom;
}
//删除一个字符; 删除光标处一个字符,光标右边的所有字符左移一格
static void delete_char(void)
{
	int i;
	unsigned short * p = (unsigned short *) pos;
//如果光标的当前列位置x 超出屏幕最右列,则返回, 否则从光标右一个字符开始行末所有字符左移
//1格, 然后再最后一个字符处填入擦除字符.
	if (x>=video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {  // 光标右所有字符左移1格
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;  // 最后填入擦除字符
}
//删除光标所在行; 删除光标所在的一行,并光标所在行开始屏幕内容上卷一行
static void delete_line(void)
{
	int oldtop,oldbottom;
//保存屏幕卷动开始行top和最后行bottom值, 然后从光标所在行让屏幕内容向上滚动一行.最后恢复
//屏幕卷动开始行top和最后行bottom的原来值.
	oldtop=top;
	oldbottom=bottom;
	top=y;//设置屏幕卷动开始行和最后行
	bottom = video_num_lines;
	scrup();//从光标开始处,屏幕内容向上滚动一行
	top=oldtop;
	bottom=oldbottom;
}
//光标处插入nr个字符; ANSI转义字符序列: ESC[Pn @, 在当前光标处插入1个或多个空格字符.Pn
//是插入的字符数. 默认是1. 光标将仍然处于第一个插入的空格字符处. 在光标与右边界的字符将右
//移. 超过右边界的字符将被丢弃. 参数nr=转义字符序列中的参数n
static void csi_at(unsigned int nr)
{
//如果插入的字符数大于一行字符数,则截为一行字符数; 若插入字符数nr=0, 则插入1个字符, 然后循环
//插入指定个空格字符
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char();
}
//在光标位置处插入 nr 行. ANSI转义字符序列: ESC[Pn L, 该控制序列在光标处插入1行或多行空行
//操作完成后光标位置不变. 当空行被插入时, 光标以下滚动区域内的行向下移动, 滚动出显示页的行
//就丢失. 参数 nr = 转义字符序列中的参数Pn
static void csi_L(unsigned int nr)
{
//如果插入的行数>屏幕最多行数,则截为屏幕显示行数; 若插入行数 nr=0,则插入1行.然后循环插入
//指定行数 nr 的空行	
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line();
}
//删除光标处的 nr 个字符, ANSI转义序列: ESC[Pn P, 该控制序列从光标处删除Pn个字符, 当一个
//字符被删除时,光标右所有字符都左移. 这会在右边界处产生一个空字符. 其属性应该与最后一个左移
//字符相同. 这里做了简化处理, 仅使用字符的默认属性(黑底白字空格0x0720)来设置空字符.
//参数 nr=转义字符序列中的参数Pn
static void csi_P(unsigned int nr)
{
//如果删除的字符数>一行字符数, 则截为一行字符数; 若删除字符数nr=0,则删除1个字符.然后循环
//删除光标处指定字符数nr	
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char();
}
//删除光标处nr行. ANSI转义字符序列 ESC[Pn M, 该控制序列在滚动区域内, 从光标所在行开始删除
//1行或多行,当行被删除时, 滚动区域内的被删行以下的行会向上移动,且会在最低行添加1空行. 若Pn
//大于显示页上剩余行数, 则本序列仅删除这些剩余行,并对滚动区域外不起作用. 
//参数nr=转义字符序列中的参数Pn
static void csi_M(unsigned int nr)
{
//如果删除的行数>屏幕最多行数,则截为屏幕显示行数; 若欲删除的行数 nr=0,则删除1行.然后循环
//删除指定行数nr
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line();
}

static int saved_x=0; //保存的光标列号;
static int saved_y=0; //保存的光标行号.
//保存当前光标位置
static void save_cur(void)
{
	saved_x=x;
	saved_y=y;
}
//恢复保存的光标位置
static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}
//控制台写函数; 从终端对应的tty写缓冲队列中取字符, 针对每个字符进行分析,若是控制字符或转义
//或控制序列,则进行光标定位、字符删除等的控制处理; 对于普通字符就直接在光标处显示.
//参数tty时当前控制台使用的tty结构指针
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;
//首先取得写缓冲队列中现有字符数nr, 然后针对队列中的每个字符进行处理.在处理每个字符的循环
//过程中,首先从写队列中取一个字符c,根据前面处理字符所设置的状态state分步骤进行处理, 状态
//之间的转换关系是: state= 0: 初始状态, 或者原是状态4; 或者原是状态1,但字符不是[
//1:原是状态0,且字符是转义字符ESC(0x1b=033=27), 处理后恢复状态0;
//2:原是状态1,且字符是[; 3:原是状态2,或原是状态3,且字符是;或数字.
//4: 原是状态3,且字符不是';' 或数字, 处理后恢复状态0
	nr = CHARS(tty->write_q);
	while (nr--) {
		GETCH(tty->write_q,c);
		switch(state) {
//如果从写队列中取出的字符是普通显示字符代码, 就直接从当前映射字符集中取出对应的显示字符,
//并放到当前光标所处的显示内存位置处, 即直接显示该字符. 然后把光标位置右移动一个字符位置
//如果字符不是控制字符也不是扩展字符,即(31<c<127), 那么若当前光标处在行末端或末端以外,
//则光标移到下行头列,并调整光标位置对应的内存指针pos. 然后将字符c写道显示内存中pos处,并
//将光标右移1列,同时将pos对应地移动2个字节.
			case 0:
				if (c>31 && c<127) { // 是普通显示字符
					if (x>=video_num_columns) {  // 要换行?
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					__asm__("movb _attr,%%ah\n\t" // 写字符
						"movw %%ax,%1\n\t"
						::"a" (c),"m" (*(short *)pos)
						:"ax");
					pos += 2;
					x++;
				} else if (c==27)//c是转义字符ESC,则转换状态state到1, ESC转义控制字符
					state=1;
				else if (c==10 || c==11 || c==12)//c是换行符(LF=10),垂直制表符VT(11)换页符FF(12),则光标移动到下1行
					lf();
				else if (c==13)//c是回车符CR(13),则将光标移动到头列(0列)
					cr();
				else if (c==ERASE_CHAR(tty))//c是DEL(127),则将光标左边字符擦除(用空格代替),并将光标移到被擦除位置
					del();
				else if (c==8) {//c=BS(backspace,8),则将光标左移1格,并相应调整光标对应内存位置指针pos
					if (x) {
						x--;
						pos -= 2;
					}
				} else if (c==9) {//c是水平制表符HT(9),则将光标移到8的倍数列上.若此时光标
//列数超出屏幕最大列数,则将光标移到下一行上.
					c=8-(x&7);
					x += c;
					pos += c<<1;
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
				} else if (c==7)//c是响铃符BEL(7),则调用蜂鸣函数,使扬声器发声
					sysbeep();
				break;
//如果在原状态0收到转义字符ESC(0x1b=033=27),则转到状态1处理.该状态对C1中控制字符或转义
//字符进行处理,处理完后默认的状态是0
			case 1:
				state=0;
				if (c=='[')//字符c='[', 则将状态转到2, ESC [ 是CSI序列
					state=2;
				else if (c=='E')//c='E', 则光标移到下一行开始处(0列), ESC E 光标下移1行回0列
					gotoxy(0,y+1);
				else if (c=='M') //c='M',则光标上移一行, ESC M 光标上移1行
					ri();
				else if (c=='D')//c='D', 光标下移一行, ESC D 光标下移1行
					lf();
				else if (c=='Z')  //c='Z', 发送终端应答字符序列 ESC Z 设备属性查询
					respond(tty);
				else if (x=='7') //c='7', 则保存当前光标位置, ESC 7 保存光标位置
					save_cur();
				else if (x=='8')//c='8', 则恢复到原保存的光标位置, ESC 8 恢复保存的光标原位置
					restore_cur();
				break;
			case 2:
//如果在状态1(是转义字符ESC)时收到字符'[', 则表明是一个控制序列引导码CSI,于是转到这里状态
//2来处理. 首先对ESC转义字符序列保存参数的数组par[]清零,索引变量npar指向首项,且设置状态为3
//若此时字符不是?, 则直接转到状态3去处理,若此时字符是?, 则说明这个序列是终端设备私有序列,
//后面会有一个功能字符,于是去读下一个字符,再到状态3处理代码处, 否则直接进入状态3继续处理
				for(npar=0;npar<NPAR;npar++)
					par[npar]=0;
				npar=0;
				state=3;
				if (ques=(c=='?'))
					break;
			case 3:
//状态3用于把转义字符序列中的数字字符转换成数值保存在par[]数组中. 如果原是状态2,或者原来就是
//状态3, 但原字符是';'或数字,则在状态3处理, 此时如果字符c是分号';', 且数组未满,则索引值+1
//准备处理下一个字符
				if (c==';' && npar<NPAR-1) {
					npar++;
					break;
				} else if (c>='0' && c<='9') {
//字符是'0'-'9', 则将该字符转换成数值并与npar所索引的项组成10进制数,并准备处理下一个字符
//否则就直接转到状态4
					par[npar]=10*par[npar]+c-'0';
					break;
				} else state=4;
			case 4:
//状态4是处理转义字符序列的最后一步,根据前面几个状态的处理,已经获得了转义字符序列的前几部分
//现在根据参数字符串中最后一个字符(命令)来执行相关的操作, 如果原状态是3且字符不是';'或数字,
//则转到状态4处理,首先复位状态state=0
				state=0;
				switch(c) {
					case 'G': case '`'://c='G'或'`',则par[0]代表列号;若列号!=0,则将光标左移1格
						if (par[0]) par[0]--;
						gotoxy(par[0],y);
						break;
					case 'A'://字符是'A'则par[0]代表光标上移的行数,若参数=0则上移1行.
						if (!par[0]) par[0]++;
						gotoxy(x,y-par[0]);
						break;
					case 'B': case 'e'://c='B'或'e', 则par[0]代表光标下移的行数,若参数=0在下移1行
						if (!par[0]) par[0]++;
						gotoxy(x,y+par[0]);
						break;
					case 'C': case 'a'://c='C'或'a', par[0]代表光标右移的格数,若参数为0则右移1格
						if (!par[0]) par[0]++;
						gotoxy(x+par[0],y);
						break;
					case 'D'://c='D',par[0]左移的格数, =0时,则左移1格
						if (!par[0]) par[0]++;
						gotoxy(x-par[0],y);
						break;
					case 'E'://c='E',par[0]下移动的行数,并回到0列.若参数=0则下移1行
						if (!par[0]) par[0]++;
						gotoxy(0,y+par[0]);
						break;
					case 'F'://c='F',par[0]代表光标向上移动的行数,并回到0列.若参数=0则上移1行
						if (!par[0]) par[0]++;
						gotoxy(0,y-par[0]);
						break;
					case 'd'://c='d', 则在当前列设置行位置, par[0]代表光标所需在的行号(从0计数)
						if (par[0]) par[0]--;
						gotoxy(x,par[0]);
						break;
					case 'H': case 'f'://c='H','f', par[0]代表光标移动的行号; par[1]代表光标移动的列号
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(par[1],par[0]);
						break;
					case 'J'://c='J',par[0]表示以光标所处位置清屏的方式, 
//转义序列: ESC [sJ s=0删除光标到屏幕底端,1删除屏幕开始到光标处; 2整屏删除
						csi_J(par[0]);
						break;
					case 'K'://c='K',par[0]表示以光标所在位置对行中字符进行删除处理的方式
//ESC [sK, s=0删除到行尾; 1从开始删除; 2:整行都删除
						csi_K(par[0]);
						break;
					case 'L'://c='L', 在光标位置处插入n行, ESC [Pn L
						csi_L(par[0]);
						break;
					case 'M'://c='M',在光标位置处删除n行,ESC [ Pn M
						csi_M(par[0]);
						break;
					case 'P'://c='P',在光标处删除n格字符, ESC [ Pn p
						csi_P(par[0]);
						break;
					case '@'://在光标处插入n格字符, ESC [ Pn @
						csi_at(par[0]);
						break;
					case 'm'://改变光标处字符的显示属性,如:加粗、加下划线、闪烁、反显等
//ESC [Pn m, n=0正常显示; 1加粗,4加下划线; 7 反显; 27正常显示					
						csi_m();
						break;
					case 'r'://用两个参数设置滚屏的起始行号和终止行号
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0];
							bottom=par[1];
						}
						break;
					case 's'://保存当前光标所在位置
						save_cur();
						break;
					case 'u'://恢复光标到原保存位置处
						restore_cur();
						break;
				}
		}
	}
	set_cursor();//根据上面设置的光标位置,向显示控制器发送光标显示位置
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *初始化控制台中断,其他什么也不做. 如果想屏幕干净,就使用适当的转义字符序列调用tty_write()
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 * 读取setup.s保存的信息,用以确定当前显示器类型,且设置所有相关参数
 */
//控制台初始化程序. 该函数首先根据setup.s取得的系统硬件参数初始化设置几个本函数专用的静态
//全局变量. 然后根据显示卡模式(单色还是彩色)和显示卡类型(EGA/VGA,CGA)分别设置显示内存起始
//位置以及显示索引寄存器和显示数值寄存器端口号. 最后设置键盘中断陷阱描述符并复位对键盘中断的
//屏蔽位,以允许键盘开始工作. 定义了一个寄存器变量a, 以便于高效访问和操作.
void con_init(void)
{
	register unsigned char a; // 若想指定存放的寄存器,则可以写成 register unsigned char a asm("ax");
	char *display_desc = "????";
	char *display_ptr;
//根据setup.s程序取得的系统硬件参数,初始化几个本函数专用的静态全局变量.
	video_num_columns = ORIG_VIDEO_COLS; //显示器显示字符列数
	video_size_row = video_num_columns * 2;  //每行字符需要使用的字节数
	video_num_lines = ORIG_VIDEO_LINES;//显示器显示字符行数
	video_page = ORIG_VIDEO_PAGE;//当前显示页面
	video_erase_char = 0x0720;//擦除字符(0x20是字符, 0x07属性)
//根据显示模式是单色、彩色分别设置所使用的显示内存起始位置以及显示寄存器索引端口和显示
//寄存器数据端口号. 如果原始显示模式=7, 则表示是单色显示器
	if (ORIG_VIDEO_MODE == 7)			/* Is this a monochrome display? */
	{
		video_mem_start = 0xb0000; //设置单显映像内存起始地址
		video_port_reg = 0x3b4;//设置单显索引寄存器端口
		video_port_val = 0x3b5;//设置单显数据寄存器端口
//根据BIOS中断int 0x10功能 0x12获取的显示模式信息,判断显示卡是单色显示卡还是彩色显示卡
//若使用上述中断功能所得到的BX寄存器返回值不等于0x10,则说明是EGA卡. 因此初始显示类型为EGA
//单色,虽然EGA卡上有较多显示内存,单在单色方式下最多只能利用地址范围在0xb0000-0xb8000之间
//的显存. 然后置显示器描字符串为EGAm,并会在系统初始化期间显示器描述符串将显示在屏幕的右
//上角. 注意:这里使用了bx在调用中断 int 0x10前后是否被改变的方法来判断卡的类型,若bl在中断
//调用后值被改变,表示显卡支持Ah=12h功能调用,是EGA或后推出来的VGA等类型的显示卡. 若中断
//调用返回值未变,表示显卡不支持这个功能,则说明是一般单色显示卡
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM; //设置显示类型EGA单色
			video_mem_end = 0xb8000;//设置显示内存末端地址
			display_desc = "EGAm";//设置显示描述字符串
		}
		else//BX寄存器的值=0x10,则说明是单色显示卡MDA, 则设置相应参数
		{
			video_type = VIDEO_TYPE_MDA;//设置显示类型MDA单色
			video_mem_end	= 0xb2000;//设置显示内存末端地址
			display_desc = "*MDA";//设置显示描述字符串
		}
	}
//显示模式不为7,说明是彩色显示卡.此时文本方式下所用显示内存起始地址为0xb8000,显示控制索引
//器端口地址=0x3d4,数据寄存器端口地址=0x3d5	
	else								/* If not, it is color. */
	{
		video_mem_start = 0xb8000;//显示内存起始地址
		video_port_reg	= 0x3d4;//设置彩色显示索引寄存器端口
		video_port_val	= 0x3d5;//设置彩色显示数据寄存器端口
//判断显卡类型,如果bx!=0x10,则说明是EGA/VGA显卡,此时可以使用32k显存(0xb8000-0xc0000),
//但该程序只使用了其中16K显存
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAC;//设置显示类型EGA彩色
			video_mem_end = 0xbc000;//设置显示内存末端地址
			display_desc = "EGAc";//设置显示描述字符串
		}
		else//BX=0x10, 说明是CGA显卡,只使用8k显存
		{
			video_type = VIDEO_TYPE_CGA;//设置显示类型 CGA
			video_mem_end = 0xba000;//设置显示内存末端地址
			display_desc = "*CGA";//设置显示描述字符串
		}
	}

	/* Let the user known what kind of display driver we are using */
//在屏幕的右上角显示描述字符串,方法是:直接将字符串写到显存的相应位置处.首先将显示指针
//display_ptr指到屏幕第1行右端差4格字符处(每个字符2格字节,因此-8),然后循环赋值字符串的
//字符,并且每复制1格字符都空开1个属性字节.
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	while (*display_desc)
	{
		*display_ptr++ = *display_desc++;//复制字符
		display_ptr++;//跳过属性字节位置
	}
	
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	//初始化用于滚屏的变量(主要用于EGA/VGA)
	origin	= video_mem_start;//滚屏起始显示内存地址.
	scr_end	= video_mem_start + video_num_lines * video_size_row;//结束地址
	top	= 0;//最顶行号
	bottom	= video_num_lines;//最底行号
//初始化当前光标所在位置和光标对应的内存位置pos. 并设置键盘中断0x21陷阱门描述符,&keyboard_interrupt
//是键盘中断处理过程地址,取消8259A中对键盘中断的屏蔽,允许响应键盘发出的IRQ1请求信号.最后
//复位键盘控制器以允许键盘开始正常工作
	gotoxy(ORIG_X,ORIG_Y);
	set_trap_gate(0x21,&keyboard_interrupt);
	outb_p(inb_p(0x21)&0xfd,0x21);//取消对键盘中断的屏蔽,允许IRQ1
	a=inb_p(0x61);//读取键盘端口0x61(8255A端口PB)
	outb_p(a|0x80,0x61);//设置禁止键盘工作(位7置位)
	outb(a,0x61);//再允许键盘工作,用以复位键盘
}
/* from bsd-net-2: */
//停止蜂鸣, 复位8255A PB端口的位1和位0.
void sysbeepstop(void)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);//禁止定时器2
}

int beepcount = 0;//蜂鸣时间滴答计数
//开通蜂鸣; 8255A芯片pB端口的位1 用作扬声器的开门信号. 位0用作8253定时器2的门信号, 该定时
//器的输出脉冲送往扬声器, 作为扬声器发声的频率, 因此要使扬声器蜂鸣,需要2步: 首先开启PB端口
//(0x61)位1和位0(置位),然后设置定时器2通道发送一定的定时频率即可.
static void sysbeep(void)
{
	/* enable counter 2 */
	outb_p(inb_p(0x61)|3, 0x61); //开启定时器2
	/* set command for counter 2, 2 byte write */ // 送设置定时器2命令
	outb_p(0xB6, 0x43);  //定时器芯片控制字寄存器端口
	/* send 0x637 for 750 HZ */ // 设置频率位750Hz,因此送定时值 0x637
	outb_p(0x37, 0x42); //通道2数据端口分别送计数高低字节
	outb(0x06, 0x42);
	/* 1/8 second */
	beepcount = HZ/8;	//蜂鸣时间位 1/8s
}
