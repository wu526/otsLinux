#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char * strerror(int errno);

/*
 * This string-include defines all string functions as inline
 * functions. Use gcc. It also assumes ds=es=data space, this should be
 * normal. Most of the string-functions are rather heavily hand-optimized,
 * see especially strtok,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *这个字符串头文件以内嵌函数的形式定义了所有字符串操作函数. 使用gcc时,同时假定了ds=es=数据空间. 这应该是常规的. 绝大多数字符串函数
 都是经过手工进行大量优化的, 尤其是strtok, strstr, str[c]span. 它们应该都能正常工作, 但却不是那么容易理解. 所有的操作基本上都是
 使用寄存器集来完成的. 这使得函数即快又整洁. 所有地方都使用了字符串指令.
 *		(C) 1991 Linus Torvalds
 */
//将一个字符串(src)拷贝到另一个字符串(dest),直到遇到NULL字符后停止; dest目的字符串指针, src:源字符串指针; %0 esi(src), %1 edi(dest)
extern inline char * strcpy(char * dest,const char *src)
{
__asm__("cld\n"
	"1:\tlodsb\n\t" //加载ds:[esi]处1字节到al, 并更新esi
	"stosb\n\t" //存储字节al -> es:[edi], 并更新edi
	"testb %%al,%%al\n\t" //刚存储的字节是0?
	"jne 1b"  //不是则向后跳转到标号1处,否则结束
	::"S" (src),"D" (dest):"si","di","ax");
return dest;  //返回目的字符串指针
}
//拷贝源字符串count个字节到目的字符串; 如果源串长度<count个字节,就附加空字符NULL到目的字符串. dest目的字符串指针; src源字符串指针
//count 拷贝字节数 %0 esi(src); %1 edi(des), %2 ecx(count)
extern inline char * strncpy(char * dest,const char *src,int count)
{
__asm__("cld\n" //清方向位
	"1:\tdecl %2\n\t"  //寄存器ecx--(即count--)
	"js 2f\n\t"  //如果count<0则向前跳转到标号2结束
	"lodsb\n\t" //取ds:[esi]处1字节到al中, 且esi++
	"stosb\n\t" //存储该字节到es:[edi], 且edi++
	"testb %%al,%%al\n\t"  // 存储的字节是0?
	"jne 1b\n\t" //不是,则向前跳转到标号1处继续拷贝
	"rep\n\t" // 否则,在目的串中存放剩余个数的空字符
	"stosb\n"
	"2:"
	::"S" (src),"D" (dest),"c" (count):"si","di","ax","cx");
return dest; //返回目的字符串指针
}
//将源字符串拷贝到目的字符串的末尾处; dest目的字符串指针; src 源字符串指针; %0 esi(src), %1 edi(dest), %2 eax(0), %3 ecx(-1)
extern inline char * strcat(char * dest,const char * src)
{
__asm__("cld\n\t"
	"repne\n\t" //比较al与es:[edi]字节,并更新edi++
	"scasb\n\t" //直到找到目的串中是0的字节,此时edi已指向后1字节
	"decl %1\n" //让es:[edi]指向0值字节
	"1:\tlodsb\n\t" //取源字符串字节ds:[esi]->al, 并esi++
	"stosb\n\t" //将该字节存到es:[edi],并edi++
	"testb %%al,%%al\n\t" //该字节是0?
	"jne 1b" //不是,则向后跳转到标号1处继续拷贝,否则结束
	::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff):"si","di","ax","cx");
return dest; //返回目的字符串指针
}
//将源字符串的count个字节复制到目的字符串的末尾处,最后添加一个空字符; dest目的字符串; src源字符串; count 要复制的字节数
//%0 esi(src), %1 edi(dest), %2 eax(0), %3 ecx(-1), %4 count
extern inline char * strncat(char * dest,const char * src,int count)
{
__asm__("cld\n\t"
	"repne\n\t" //比较al与es:[edi]字节,edi++
	"scasb\n\t" //直到找到目的串的末端0值字节
	"decl %1\n\t" //edi指向该0值字节
	"movl %4,%3\n" // 欲复制字节数->ecx
	"1:\tdecl %3\n\t" //ecx-- 从0开始计数
	"js 2f\n\t" //ecx<0? 是则向前跳转到标号2处
	"lodsb\n\t" //取ds:[esi]处的字节到al, esi++
	"stosb\n\t" //存到到es:[edi]处,edi++
	"testb %%al,%%al\n\t" //存储的字节值=0?
	"jne 1b\n"  //不是0则跳转到标号1处,继续复制
	"2:\txorl %2,%2\n\t" //al清0
	"stosb" // 存到es:[edi]处
	::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff),"g" (count)
	:"si","di","ax","cx");
return dest; //返回目的字符串指针
}
//将一个字符串与另一个字符串进行比较; cs字符串1; ct 字符串2; %0 eax(__res), %1 edi(cs) 字符串1指针; %2 esi(ct)字符串2指针
//如果串1>串2返回1; 串1=串2,返回0; 串1 < 串2返回-1；
extern inline int strcmp(const char * cs,const char * ct)
{
register int __res __asm__("ax"); //__res是寄存器变量eax
__asm__("cld\n"
	"1:\tlodsb\n\t" //取字符串2的字节ds:[esi]->al, 且esi++
	"scasb\n\t" //al与字符串1的字节es:[edi]做比较,且edi++
	"jne 2f\n\t"  // 如果不相等, 则向前跳转到标号2
	"testb %%al,%%al\n\t"  // 该字节是0值字节吗(字符串结尾)?
	"jne 1b\n\t" //不是,则向后跳转到标号1,继续比较
	"xorl %%eax,%%eax\n\t"  //是,则返回值eax清0
	"jmp 3f\n"  // 跳转到3,结束
	"2:\tmovl $1,%%eax\n\t" //eax中置1
	"jl 3f\n\t" //若前面比较中串2字符 < 串1字符, 则返回正值结束
	"negl %%eax\n"  //否则eax=-eax, 返回负值,结束
	"3:"
	:"=a" (__res):"D" (cs),"S" (ct):"si","di");
return __res; //返回比较结果
}
//字符串1与字符串2的前count个字符进行比较; cs字符串1; ct字符串2; count比较的字符数; %0 eax(__res)返回值; %1 edi(cs串1指针)
//%2 esi(ct串2指针); %3 ecx(count); 返回: 如果串1>串2返回1; 串1=串2,返回0; 串1 < 串2返回-1；
extern inline int strncmp(const char * cs,const char * ct,int count)
{
register int __res __asm__("ax");  //__res是寄存器变量(eax)
__asm__("cld\n"
	"1:\tdecl %3\n\t" //count--
	"js 2f\n\t" //如果count<0,则向前跳转到标号2
	"lodsb\n\t"  // 取串2的字符ds:[esi]->al, esi++
	"scasb\n\t" // 比较al与串1的字符es:[edi],且edi++
	"jne 3f\n\t" //如果不相等,则向前跳转到标号3
	"testb %%al,%%al\n\t" //该字符是NULL字符吗?
	"jne 1b\n" //不是,则向后跳转到标号1,继续比较
	"2:\txorl %%eax,%%eax\n\t" //是NULL字符,则eax清0(返回值)
	"jmp 4f\n" //向前跳转到标号4,结束
	"3:\tmovl $1,%%eax\n\t" //eax中置1
	"jl 4f\n\t" //如果前面比较中串2字符<串1字符,则返回1结束
	"negl %%eax\n" //否则eax=-eax,返回负值,结束
	"4:"
	:"=a" (__res):"D" (cs),"S" (ct),"c" (count):"si","di","cx");
return __res; //返回比较结果
}
//在字符串中寻找第一个匹配的字符; s字符串, c要寻找的字符; %0 eax(__res); %1 esi(字符串指针s); %2 eax(字符c)
//返回字符串中第一次出现匹配字符的指针. 若没有找到匹配的字符,则返回空指针.
extern inline char * strchr(const char * s,char c)
{
register char * __res __asm__("ax");
__asm__("cld\n\t"
	"movb %%al,%%ah\n" //将要比较的字符移到ah
	"1:\tlodsb\n\t"  // 取字符串中字符ds:[esi]->al, esi++
	"cmpb %%ah,%%al\n\t" // 字符串中字符al与指定字符ah比较
	"je 2f\n\t" //相等,则向前跳转到标号2处
	"testb %%al,%%al\n\t" // al中字符是NULL字符吗?
	"jne 1b\n\t" //不是,则向后跳转到标号1,继续执行
	"movl $1,%1\n" //是,则说明没有找到匹配字符,esi置1
	"2:\tmovl %1,%0\n\t" //将指向匹配字符后一个字节处的指针值放入eax
	"decl %0" //将指针调整为指向匹配的字符
	:"=a" (__res):"S" (s),"0" (c):"si");
return __res; //返回指针
}
//寻找字符串中指定字符最后一次出现的地方(反向搜索字符串); s字符串, c要寻找的字符; %0 edx(__res); %1 edx(0), %2 esi(字符串指针s)
//%3 eax(字符c); 返回字符串中最后一次出现匹配字符的指针,若没有找到匹配的字符,则返回空指针
extern inline char * strrchr(const char * s,char c)
{
register char * __res __asm__("dx");
__asm__("cld\n\t"
	"movb %%al,%%ah\n" //将要寻找的字符移到ah
	"1:\tlodsb\n\t" //取字符串总字符ds:[esi]->al, 且esi++
	"cmpb %%ah,%%al\n\t" //字符串中字符al与指定字符ah作比较
	"jne 2f\n\t" // 不相等,则向前跳转到标号2
	"movl %%esi,%0\n\t" // 将字符指针保存到edx中
	"decl %0\n" // 指针后退一位, 指向字符串中匹配字符处.
	"2:\ttestb %%al,%%al\n\t" //比较的字符是0吗?
	"jne 1b" //不是则向后跳转到标号1处,继续比较
	:"=d" (__res):"0" (0),"S" (s),"a" (c):"ax","si");
return __res; //返回指针
}
//在字符串1中寻找第一个字符序列,该序列中的任何字符都包含在字符串2中; cs字符串1指针; ct 字符串2指针
//%0 esi(__res), %1 eax(0), %2 exc(-1), %3 esi(cs串1指针); %4 ct(串2指针)
//返回字符串1中包含字符串2中任何字符的首个字符序列的长度值
extern inline int strspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t" // 首先计算串2的长度, 串2指针放入edi
	"repne\n\t" // 比较al(0)与串2中的字符(es:[edi]), 且edi++
	"scasb\n\t"  //如果不相等就继续比较(ecx逐步递减)
	"notl %%ecx\n\t" //ecx中每位取反
	"decl %%ecx\n\t"  //ecx--, 得串2的长度
	"movl %%ecx,%%edx\n" //将串2的长度值暂时放入edx中
	"1:\tlodsb\n\t" // 取串1字符 ds:esi->al, 且esi++
	"testb %%al,%%al\n\t"  // 该字符等于0值吗?(串1结尾?)
	"je 2f\n\t"  // 如果是,则向前跳转到标号2处
	"movl %4,%%edi\n\t"  // 取串2头指针放入edi中
	"movl %%edx,%%ecx\n\t" //再将串2的长度放入ecx中
	"repne\n\t" //比较al与串2中字符es:[edi],且edi++
	"scasb\n\t"  //如果不等继续比较
	"je 1b\n" //如果相等,则向后跳转到标号1处
	"2:\tdecl %0" //esi--,指向最后一个包含在串2中的字符
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res-cs;  //返回字符序列的长度值
}
//寻找字符串1中不包含字符串2中任何字符的首个字符序列. cs-字符串1指针; ct字符串2指针; %0 esi(__res), %1 eax(0), %3 esi(串1指针cs);
//%4 ct(串2指针); 返回字符串1中包含字符串2中任何字符的首个字符序列的长度值
extern inline int strcspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t" //首先计算串2的长度,串2指针放入edi中;
	"repne\n\t"  //比较al(0)与串2中的字符 es:[edi], 且edi++
	"scasb\n\t" //如果不相等就继续比较(ecx逐步递减)
	"notl %%ecx\n\t" // ecx中每位取反
	"decl %%ecx\n\t" // exc--, 得串2的长度值
	"movl %%ecx,%%edx\n" //将串2的长度值暂时放入edx中
	"1:\tlodsb\n\t" //取串1字符 ds:[esi]->al, 且esi++
	"testb %%al,%%al\n\t" // 该字符等于0值吗?
	"je 2f\n\t" //如果是,则向前跳转到标号2处
	"movl %4,%%edi\n\t" // 取串2头指针放入edi
	"movl %%edx,%%ecx\n\t" //再将串2的长度放入ecx
	"repne\n\t" //比较al与串2中字符es:[edi],且edi++
	"scasb\n\t" //如果不相等就继续比较
	"jne 1b\n" // 如果不相等则向后跳转到标号1处
	"2:\tdecl %0" //esi--, 指向最后一个包含在串2中的字符
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res-cs; //返回字符序列的长度值
}
//在字符串1中寻找首个包含字符串2中的任何字符; cs字符串1的指针; ct字符串2的指针; %0 esi(__res), %1 eax(0), %2 ecx(-1), %3 esi(cs串1指针)
//%4 ct(字符串2指针);  返回字符串1中首个包含字符串2中字符的指针
extern inline char * strpbrk(const char * cs,const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t" //首先计算串2的长度, 串2指针放入edi中
	"repne\n\t" //比较al(0)与串2中的字符(es:[edi]),且edi++
	"scasb\n\t" //如果不相等就继续比较(ecx逐步递减)
	"notl %%ecx\n\t" //ecx中每位取反
	"decl %%ecx\n\t" // ecx--, 取得串2的长度
	"movl %%ecx,%%edx\n" //将串2的长度值放入edx中
	"1:\tlodsb\n\t" // 取串1字符ds:[esi]->al且esi++
	"testb %%al,%%al\n\t" //如果字符等于0值吗?
	"je 2f\n\t" // 如果是,则向前跳转到标号2处
	"movl %4,%%edi\n\t" //取串2头指针放入edi中
	"movl %%edx,%%ecx\n\t" // 将串2的长度放入ecx
	"repne\n\t" //比较al与串2中字符es:[edi],且edi++
	"scasb\n\t" // 如果不相等就继续比较
	"jne 1b\n\t" //如果不等则向后跳转到标号1处
	"decl %0\n\t" //esi--, 指向一个包含在串2中的字符
	"jmp 3f\n" //向前跳转到标号3处
	"2:\txorl %0,%0\n" // 没有找到符号条件的,返回值=NULL
	"3:"
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res; //返回指针值
}
// 在字符串1中寻找首个匹配整个字符串2的字符串; cs字符串1指针, ct字符串2指针; %0 eax(__res), %1 eax(0), %2 exc(-1), %3 esi(cs串1指针)
//%4 ct串2指针; 返回: 返回字符串1中首个匹配字符串2的字符串指针
extern inline char * strstr(const char * cs,const char * ct)
{
register char * __res __asm__("ax");
__asm__("cld\n\t" \
	"movl %4,%%edi\n\t" //首先计算串2的长度, 串2指针放入edi中
	"repne\n\t" //比较al(0)与串2中的字符(es:edi), edi++
	"scasb\n\t" // 如果不相等就继续比较ecx逐步递减
	"notl %%ecx\n\t" //ecx 取反
	"decl %%ecx\n\t"	/* NOTE! This also sets Z if searchstring='' */ //如果搜索串为空,将设置Z标志, 得串2的长度值
	"movl %%ecx,%%edx\n" // 将串2的长度值放入edx
	"1:\tmovl %4,%%edi\n\t" // 取串2头指针放入edi
	"movl %%esi,%%eax\n\t" //将串1的指针复制到eax
	"movl %%edx,%%ecx\n\t" //将串2的长度放入ecx
	"repe\n\t" //比较串1和串2字符 ds:esi, es:edi, esi++, edi++
	"cmpsb\n\t" //若对应字符相等就一直比较下去
	"je 2f\n\t"		/* also works for empty string, see above */ //对空串同样有效, 若全等,则跳转到标号2
	"xchgl %%eax,%%esi\n\t" //串1头指针 ->esi, 比较结果的串1指针->eax
	"incl %%esi\n\t" //串1头指针指向下一个字符
	"cmpb $0,-1(%%eax)\n\t" //串1头指针(eax-1)所指字节是0吗
	"jne 1b\n\t" //不是则转到标号1,继续从串1的第2个字符开始比较
	"xorl %%eax,%%eax\n\t" //清eax,表示没有找到匹配
	"2:"
	:"=a" (__res):"0" (0),"c" (0xffffffff),"S" (cs),"g" (ct)
	:"cx","dx","di","si");
return __res; //返回比较结果
}
//计算字符串长度; s-字符串; %0 ecx(__res), %1 edi (s字符串指针); %2 ecx (-1); 返回字符串的长度
extern inline int strlen(const char * s)
{
register int __res __asm__("cx");
__asm__("cld\n\t"
	"repne\n\t" //al(0)与字符串中字符es:[edi]比较
	"scasb\n\t" //若不等就一直比较
	"notl %0\n\t" //ecx取反
	"decl %0" //ecx--,得字符串的长度值
	:"=c" (__res):"D" (s),"a" (0),"0" (0xffffffff):"di");
return __res;  //返回字符串长度值
}

extern char * ___strtok; //用于临时存放指向下面被分析字符串1(s)的指针
//利用字符串2中的字符将字符串1分割成标记(tokern)序列
//将串1看作是包含0个或多个单词(token)的序列, 并由分割符字符串2中的一个或多个字符分开. 第一次调用strtok()时,将返回指向字符串1中第1个
//token首字符的指针,并在返回token时将一null字符写到分割符处. 后续使用null作为字符串1的调用,将用这种方法继续扫描字符串1,直到没有token
//为止, 在不同的调用过程中,分隔符串2可以不同. s-待处理的字符串1; ct包含各个分割符的字符串2; 汇编输出: %0 ebx(__res), %1 esi(__strtok);
//汇编输入: %2 ebx(__strtok), %3 esi(字符串1指针cs), %4 字符串2指针ct; 返回: 返回字符串s中第1个token,如果没有找到token,则返回
//一个null指针; 后续使用字符串s指针为null的调用,将在原字符串s中搜索下一个token
extern inline char * strtok(char * s,const char * ct)
{
register char * __res __asm__("si");
__asm__("testl %1,%1\n\t" //首先测试esi(串1指针s)是否是NULL
	"jne 1f\n\t"  //不是,则表明是首次调用本函数,跳转到标号1
	"testl %0,%0\n\t" //若是NULL,表示此次是后续调用,测ebx(__strtok)
	"je 8f\n\t" //如果ebx=NULL,则不能处理,跳转到结束
	"movl %0,%1\n" // 将ebx指针复制到esi
	"1:\txorl %0,%0\n\t" //清ebx指针
	"movl $-1,%%ecx\n\t" //置ecx=0xffffffff
	"xorl %%eax,%%eax\n\t" //清0eax
	"cld\n\t" //清方向位
	"movl %4,%%edi\n\t" //求字符串2的长度,edi指向字符串2
	"repne\n\t" // 将al(0)与es:[edi]比较,且edi++
	"scasb\n\t" //直到找到字符串2的结束null字符,或计数ecx=0
	"notl %%ecx\n\t" //ecx取反
	"decl %%ecx\n\t" //ecx--, 得到字符串2的长度值
	"je 7f\n\t"			/* empty delimeter-string */ // 分割字符串空; //若串2长度=0,则跳转到标号7
	"movl %%ecx,%%edx\n" // 将串2长度存入edx
	"2:\tlodsb\n\t" // 取串1的字符 ds:[esi]->al, 且esi++
	"testb %%al,%%al\n\t" //该字符为0值吗?
	"je 7f\n\t" // 如果是,则跳转到标号7
	"movl %4,%%edi\n\t" // edi再次指向串2首
	"movl %%edx,%%ecx\n\t" // 取串2的长度值置入计数器ecx
	"repne\n\t" //将al中串1的字符与串2中所有字符比较
	"scasb\n\t" //判断该字符是否为分割符
	"je 2b\n\t" //若能在串2中找到相同字符(分割符), 则跳转标号2
	"decl %1\n\t" //不是分隔符,则串1指针esi指向此时的该字符
	"cmpb $0,(%1)\n\t" //该字符是NULL字符吗?
	"je 7f\n\t" //是,则跳转
	"movl %1,%0\n" // 将该字符的指针esi放在ebx
	"3:\tlodsb\n\t" //取串1下一个字符 ds:[esi]->al, 且esi++
	"testb %%al,%%al\n\t" //该字符是NULL吗?
	"je 5f\n\t" //是,表示串1结束,跳转到5
	"movl %4,%%edi\n\t" //edi再次指向串2首
	"movl %%edx,%%ecx\n\t" // 串2长度值置入计数器ecx
	"repne\n\t" //将al中串1的字符与串2中每个字符比较
	"scasb\n\t" //测试al字符是否是分割符
	"jne 3b\n\t" //不是分隔符则跳转标号3,检测串1中下一个字符.
	"decl %1\n\t" //是分割符,则esi--,指向该分割符字符
	"cmpb $0,(%1)\n\t" //该分割符是NULL字符吗?
	"je 5f\n\t" //是则跳转
	"movb $0,(%1)\n\t" //不是,则将该分割用NULL字符替换掉
	"incl %1\n\t" // esi指向串1中下一个字符,即剩余串首
	"jmp 6f\n" //跳转6
	"5:\txorl %1,%1\n" //esi清零
	"6:\tcmpb $0,(%0)\n\t" //ebx指针指向NULL字符吗?
	"jne 7f\n\t" //不是,则跳转到7
	"xorl %0,%0\n" // 是,则让ebx=NULL
	"7:\ttestl %0,%0\n\t" //ebx指针是NULL吗?
	"jne 8f\n\t" //不是则跳转到8, 结束汇编代码
	"movl %0,%1\n" //将esi置为NULL
	"8:"
	:"=b" (__res),"=S" (___strtok)
	:"0" (___strtok),"1" (s),"g" (ct)
	:"ax","cx","dx","di");
return __res; //返回指向新token的指针
}
//内存块复制; 从源地址src处开始复制n个字节到目的地址dest处; dest复制的目的地址; src复制的源地址; n复制字节数
//%0 ecx(n), %1 esi(src), %2 edi(dest)
extern inline void * memcpy(void * dest,const void * src, int n)
{
__asm__("cld\n\t"
	"rep\n\t"
	"movsb" //重复指向复制ecx个字节
	::"c" (n),"S" (src),"D" (dest)
	:"cx","si","di");
return dest; //返回目的地址
}
//内存块移动, 同内存块复制,但考虑移动的方向; dest 复制的目的地址; src复制的源地址; n-复制字节数; 若dest<src则 %0 ecx(n), %1 esi(src)
//%2 edi(dest); 否则: %0 ecx(n), %1 esi(src+n-1), %2 edi(dest+n-1); 这样操作是为了防止在复制时错误的重叠覆盖
extern inline void * memmove(void * dest,const void * src, int n)
{
if (dest<src)
__asm__("cld\n\t"
	"rep\n\t" //从ds:[esi]到es:[edi],且esi++, edi++
	"movsb"//重复执行复制ecx字节
	::"c" (n),"S" (src),"D" (dest)
	:"cx","si","di");
else
__asm__("std\n\t" //置方向位, 从末端开始复制
	"rep\n\t" //从ds:[esi]到es:[edi],且esi--, edi--
	"movsb" //复制ecx个字节
	::"c" (n),"S" (src+n-1),"D" (dest+n-1)
	:"cx","si","di");
return dest;
}
//比较n个字节的两块内存(两个字符串), 即使遇上NULL字节也不停止比较; cs内存块1地址; ct内存块2地址; count: 比较的字节数;
//%0 eax(__res); %1 eax(0), %2 edi(cs内存块1), %3 esi(ct内存块2); %4 ecx(count);
//返回 块1>块2则返回1; 块1<块2返回-1; 块1=块2,返回0
extern inline int memcmp(const void * cs,const void * ct,int count)
{
register int __res __asm__("ax");
__asm__("cld\n\t"
	"repe\n\t" //如果相等则重复
	"cmpsb\n\t" //比较ds:[esi]与es:[edi]的内容,且esi++, edi++
	"je 1f\n\t" //如果都相同,则跳转到标号1,返回0(eax)值
	"movl $1,%%eax\n\t" //否则eax置1
	"jl 1f\n\t" //若内存块2内容值 块1,则跳转到1
	"negl %%eax\n" //否则eax=-eax
	"1:"
	:"=a" (__res):"0" (0),"D" (cs),"S" (ct),"c" (count)
	:"si","di","cx");
return __res; //返回比较结果
}
//在n字节大小的内存块(字符串)中寻找指定字符; cs指定内存块地址; c 指定的字符; count 内存块长度; %0 edi(__res); %1 eax(字符c);
// %2 edi(内存块地址cs), %3 ecx(字节数count); 返回第一个匹配字符的指针,如果没有找到,则返回NULL字符.
extern inline void * memchr(const void * cs,char c,int count)
{
register void * __res __asm__("di");
if (!count) //如果内存块长度==0,返回NULL,没有找到
	return NULL;
__asm__("cld\n\t"
	"repne\n\t" //如果不相等则重复执行下面语句
	"scasb\n\t" //al中字符与es:[edi]字符比较, 且edi++
	"je 1f\n\t" //如果相等则向前跳转到标号1
	"movl $1,%0\n" //否则edi置1
	"1:\tdecl %0" //让edi指向找到的字符(或NULL)
	:"=D" (__res):"a" (c),"D" (cs),"c" (count)
	:"cx");
return __res; //返回字符指针
}
//用字符填写指定长度内存块; 用字符c填写s指向的内存区域,共填count字节. %0 eax(字符c); %1 edi(内存地址s), %2 ecx(字节数count)
extern inline void * memset(void * s,char c,int count)
{
__asm__("cld\n\t"
	"rep\n\t" //重复ecx指定的次数,执行
	"stosb"  //将al中字符存入es:[edi]中,且edi++
	::"a" (c),"D" (s),"c" (count)
	:"cx","di");
return s;
}

#endif
