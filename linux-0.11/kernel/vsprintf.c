/*
 *  linux/kernel/vsprintf.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
这段代码中有个bug, 直到1994年才有人发现, 并予以纠正. 这个bug是在使用*作为输出域宽度时,
忘记递增指针跳过这个星号. 本代码中这个bug还依然存在(line130)
 */

#include <stdarg.h>
#include <string.h>

/* we use this so that we can do without the ctype library */
// 使用下面的定义, 这样就可以不使用 ctype 库了
#define is_digit(c)	((c) >= '0' && (c) <= '9')
//该函数将字符串转换为整数,输入是字符串指针的指针,返回是结果数值,另外指针将前移
static int skip_atoi(const char **s)
{
	int i=0;

	while (is_digit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}
//定义转换类型的各种符号常数
#define ZEROPAD	1		/* pad with zero */ //填充0
#define SIGN	2		/* unsigned/signed long */  // 无符号/符号长整数
#define PLUS	4		/* show plus */  // 显示加号
#define SPACE	8		/* space if plus */  // 如果是加则置空格
#define LEFT	16		/* left justified */  // 左调整
#define SPECIAL	32		/* 0x */  // 0x
#define SMALL	64		/* use 'abcdef' instead of 'ABCDEF' */ // 使用小写字母
// 除操作, 输入:n被除数,base除数; 结果: n为商,函数返回值为余数.
#define do_div(n,base) ({ \
int __res; \
__asm__("divl %4":"=a" (n),"=d" (__res):"0" (n),"1" (0),"r" (base)); \
__res; })
//将整数转换为指定进制的字符串
static char * number(char * str, int num, int base, int size, int precision
	,int type)
{
	char c,sign,tmp[36];
	const char *digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int i;
// 如果类型type指出用小写字母,则定义小写字母集; 如果类型指出要左调整(靠左边界),则屏蔽类型中
//的填0标志; 如果进制基数小于2或大于36则退出处理,即本程序只能吹了基数在2-32之间的数
	if (type&SMALL) digits="0123456789abcdefghijklmnopqrstuvwxyz";
	if (type&LEFT) type &= ~ZEROPAD;
	if (base<2 || base>36)
		return 0;
// 如果类型指出要填0,则置字符变量c='0',否则c等于空格字符. 如果类型指出是带符号数且数值num
//小于0,则置符号变量sign=负号,并使num取绝对值. 否则如果类型指出是加号,则置sign=加号,否则若
//类型带空格标志则sign=空格,否则置0		
	c = (type & ZEROPAD) ? '0' : ' ' ;
	if (type&SIGN && num<0) {
		sign='-';
		num = -num;
	} else
		sign=(type&PLUS) ? '+' : ((type&SPACE) ? ' ' : 0);
//若带符号,则宽度值-1, 若类型指出是特殊转换, 则对16进制宽度再减少2为(用于0x)
//对于8进制宽度-1(用于8进制转换结果前放一个0)
	if (sign) size--;
	if (type&SPECIAL)
		if (base==16) size -= 2;
		else if (base==8) size--;
//如果数值num=0,则临时字符串='0', 否则根据给定的基数将数值num转换成字符形式.		
	i=0;
	if (num==0)
		tmp[i++]='0';
	else while (num!=0)
		tmp[i++]=digits[do_div(num,base)];
//若数值字符个数大于精度值,则精度值扩展为数字个数值. 宽度指size减去用于存放数值字符的个数		
	if (i>precision) precision=i;
	size -= precision;
// 从这里真正开始形成所需要的转换结果,且暂时放在字符串str中. 若类型中没有填零或左靠齐标志,
//则在str中首先填放剩余宽度值指出的空格数,若需带符号位,则存入符号.
	if (!(type&(ZEROPAD+LEFT)))
		while(size-->0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	if (type&SPECIAL)//若类型指出是特殊转换,则对于8进制转结果头一位放一个0,16进制放0x
		if (base==8)
			*str++ = '0';
		else if (base==16) {
			*str++ = '0';
			*str++ = digits[33];
		}
	if (!(type&LEFT)) //类型中没有左调整,则在剩余宽度中存放c字符(0或空格)
		while(size-->0)
			*str++ = c;
	while(i<precision--)// 此时i存有数值num的数字个数,若数字个数小于精度值,则str中放入(精度值-i)个0
		*str++ = '0';
	while(i-->0)//将转换好的数字字符填入str中,共i个
		*str++ = tmp[i];
	while(size-->0)  // 宽度值仍大于0, 则表示类型标志中有左靠齐标志,则在剩余宽度中放空格
		*str++ = ' ';
	return str; //返回转换好的字符串.
}
//送格式化输出到字符串中. 为了能子内核中使用格式化的输出,在内核中实现了该c标准库函数
//fmt是格式字符串. args:是个数变化的值. buf 是输出字符串缓冲区.
int vsprintf(char *buf, const char *fmt, va_list args)
{
	int len;
	int i;
	char * str;  // 用于存放转换过程中的字符串
	char *s;
	int *ip;

	int flags;		/* flags to number() */ //number()函数使用的标志

	int field_width;	/* width of output field */ // 输出字段宽度
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string min:整数数字个数,max:字符串中字符个数 */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields, h,l,L用于整数字段 */
//首先将字符串指向buf,然后扫描格式字符串,对各个格式转换指示进行相应的处理
	for (str=buf ; *fmt ; ++fmt) {  // line107
		if (*fmt != '%') {//格式转换指示符均以%开始,这里从fmt格式字符串中扫描%,寻找格式转换
		//字符串的开始, 不是格式指示的一般字符均被依次存入str
			*str++ = *fmt;
			continue;
		}
//取得格式字符串中的标志域,并将标志常量放入flags变量中			
		/* process flags */
		flags = 0;
		repeat:
			++fmt;		/* this also skips first '%' */
			switch (*fmt) {
				case '-': flags |= LEFT; goto repeat;//左靠齐调整
				case '+': flags |= PLUS; goto repeat; //放加号
				case ' ': flags |= SPACE; goto repeat;  // 放空格
				case '#': flags |= SPECIAL; goto repeat; // 特殊转换
				case '0': flags |= ZEROPAD; goto repeat; // 要填0
				}
//取当前参数字段宽度域值, 放入field_width变量中,如果宽度域中是数值则直接取其为宽度值.如果
//宽度域中是字符*,表示下一个参数指定宽度,因此调用va_arg取宽度值.若此时宽度值<0,则该负数表示
//其带有标志域"-"标志(左靠齐), 因此还需要再标志变量中填入该标志,并将字段宽度值取为其绝对值
		/* get field width */
		field_width = -1;
		if (is_digit(*fmt))
			field_width = skip_atoi(&fmt);
		else if (*fmt == '*') {
			/* it's the next argument */ //line130 这里有个bug,应该加入 ++fmt;
			field_width = va_arg(args, int);
			if (field_width < 0) {
				field_width = -field_width;
				flags |= LEFT;
			}
		}
//这段代码取格式转换串的精度域,并放入precision变量中,精度域开始的标志是'.' 其处理过程与
//上面宽度类似.如果精度域中数值直接取为精度值,如果精度域中是字符*,则表示下一个参数指定
//精度,因此调用va_arg取精度值.若此时宽度值<0,则将字段精度值取为0
		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;	
			if (is_digit(*fmt))
				precision = skip_atoi(&fmt);
			else if (*fmt == '*') {
				/* it's the next argument */ // 同理这里也应该插入 ++fmt;
				precision = va_arg(args, int);
			}
			if (precision < 0)
				precision = 0;
		}
//分析长度修饰符,并将其存入 qualifer变量.
		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
		}
//分析转换指示符
		switch (*fmt) {
//如果转换指示符是c, 则表示对应参数应是字符.此时如果标志域表明不是左靠齐,则该字段前面放入
//宽度域值-1个空格字符,然后再放入参数字符.如果宽度域还大于0,则表示为左靠齐,则在参数字符后面
//添加 宽度值 -1 个空格
		case 'c':
			if (!(flags & LEFT))
				while (--field_width > 0)
					*str++ = ' ';
			*str++ = (unsigned char) va_arg(args, int);
			while (--field_width > 0)
				*str++ = ' ';
			break;
//转换指示符是s, 则表示对应参数是字符串, 首先取参数字符串的长度,若其超过了精度域值,则扩展
//精度域=字符串长度. 此时如果标志域表明不是左靠齐,则该字段前放入 宽度值-字符串长度 个空格
//字符. 然后再放入参数字符串. 如果宽度域还大于0, 则表示为左靠齐,则再参数字符串后面添加
// 宽度值 - 字符串长度 个空格字符
		case 's':
			s = va_arg(args, char *);
			len = strlen(s);
			if (precision < 0)
				precision = len;
			else if (len > precision)
				len = precision;

			if (!(flags & LEFT))
				while (len < field_width--)
					*str++ = ' ';
			for (i = 0; i < len; ++i)
				*str++ = *s++;
			while (len < field_width--)
				*str++ = ' ';
			break;
//表示需要将对应的参数转换为8进制, 调用number()处理
		case 'o':
			str = number(str, va_arg(args, unsigned long), 8,
				field_width, precision, flags);
			break;
//对应参数是一个指针类型,此时若该参数没有设置宽度域,则默认宽度为8且需要添0,然后调用numbe()
		case 'p':
			if (field_width == -1) {
				field_width = 8;
				flags |= ZEROPAD;
			}
			str = number(str,
				(unsigned long) va_arg(args, void *), 16,
				field_width, precision, flags);
			break;
//x或X, 则表示打印成16进制, x表示用小写字母表示
		case 'x':
			flags |= SMALL;
		case 'X':
			str = number(str, va_arg(args, unsigned long), 16,
				field_width, precision, flags);
			break;
//d,i,u表示对应参数是整数, d,i代表符号整数,因此需要加上带符号标志, u代表无符号整数
		case 'd':
		case 'i':
			flags |= SIGN;
		case 'u':
			str = number(str, va_arg(args, unsigned long), 10,
				field_width, precision, flags);
			break;
//若格式转换指示符是n,则表示要把目前为止转换输出字符数保存到对应参数指针指定的位置中.首先
//利用va_arg()取得该参数指针,然后将已经转换好的字符数存入该指针所指的位置
		case 'n':
			ip = va_arg(args, int *);
			*ip = (str - buf);
			break;
//若格式转换符不是%,则表示格式字符串有错误,直接将一个%写入输出串中.如果格式转换符的位置处
//还有字符,则也直接将该字符写入输出串中,并返回到line107 继续处理格式字符串.否则表示
//已经处理到格式字符串的结尾处, 则退出循环
		default:
			if (*fmt != '%')
				*str++ = '%';
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			break;
		}
	}
	*str = '\0';  // 最后再转换好的字符串结尾处添上null
	return str-buf;  // 返回转换好的字符串长度值
}
