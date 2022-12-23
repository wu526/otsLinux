/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
//使用volatile修饰函数, 告诉编译器该函数不会有返回值, 可以让gcc产生更好一些的代码.
volatile void do_exit(long code);
//显示内存已用完出错信息,并退出
static inline volatile void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV); //应该使用退出代码,这里用了信号值SIGSEGV(11)相同值的出错码含义:资源暂不可用
}
//刷新页变换高速缓冲宏函数. 为了提高地址转换的效率, cpu将最近十一点页表数据存放在芯片中高速缓冲中
//在修改页表信息后,就需要刷新该缓冲区,这里使用重新加载页目录基地寄存器cr3的方法来进行刷新,
//eax=0是页目录的基址
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
//下面定义若需要改动,则需要与head.s等文件中的相关信息一起改变; linux0.11默认支持的最大内存是16M,
//可以修改这些定义以适合更多的内存
#define LOW_MEM 0x100000  //内存低端(1M)
#define PAGING_MEMORY (15*1024*1024) //分页内存15M, 主内存区最多15M
#define PAGING_PAGES (PAGING_MEMORY>>12) //分页后的物理内存页面数3840
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12) //指定内存地址映射为页号
#define USED 100  //页面被占用标志
//该宏用于判断给定线性地址是否位于当前进程的代码段中 (addr+4095) &~4095 用于取得线性地址addr所在内存页面的末端地址
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0; //全局变量,存放实际物理内存最高端地址
//从from处复制1页内存到to处(4k字节)
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")
//物理内存映射字节图(1字节代表1页内存). 每个页面对应的字节用于标志页面当前被引用(占用)的次数
//最大可映射15M内存空间. 在初始化函数 mem_init()中对于不能用作主内存区页面的位置均预先被设置成
//USED(100)
static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
//获取首个(实际上是最后一个)空闲页面,并标记为以使用.如果没有空闲页面就返回0.
//在主内存中取空闲物理页面,如果已经没有可用物理内存页面,则返回0. %1 (ax=0) 0, %2(LOW_MEM)内存
//字节位图管理的起始位置; %3(cx=PAGING_PAGES); %4 edi=mem_map+PAGING_PAGES-1;
//返回%0(ax=物理页面起始地址); %4寄存器实际指向mem_map[]内存字节位图的最后一个字节. 本函数从
//位图末端开始向前扫描所有页面标志(页面总数=PAGING_PAGES), 若有页面空闲(内存位图字节=0)则返回
//页面地址. 本函数只是指出在主内存区的一页空闲物理页面,并没有映射到某个进程的地址空间中去,
//后面的put_page()用于把指定页面映射到某个进程的地址空间中. 对于内核使用本函数并不需要再使用
//put_page()进行映射, 因为内核代码和数据空间(16M)已经对等地映射到物理地址空间
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t" //置方向位, al(0)与对应每个页面的(di)内容比较
	"jne 1f\n\t" //没有等于0的字节,则跳转结束(返回0)
	"movb $1,1(%%edi)\n\t" // 1=>[1+edi], 将对应页面内存映射比特置1
	"sall $12,%%ecx\n\t" // 页面数*4k=相对页面起始地址
	"addl %2,%%ecx\n\t"  //再加上低端内存地址,得页面实际物理起始地址
	"movl %%ecx,%%edx\n\t"  //将页面实际起始地址 -> edx寄存器
	"movl $1024,%%ecx\n\t"  // 寄存器ecx置计算值1024
	"leal 4092(%%edx),%%edi\n\t" //将4092 + edx的位置 -> edi 该页面的末端
	"rep ; stosl\n\t"  // 将edi所指内存清0(反方向,即将该页面清零)
	"movl %%edx,%%eax\n" //将页面起始地址 -> eax 返回值
	"1:"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	:"di","cx","dx");
return __res; //返回空闲物理页面地址(若无空闲页面则返回0)
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
//释放物理地址 addr 开始的1页内存; 物理地址1M以下的内存空间用于内核程序和缓冲,不作为分配页面
//的内存空间, 因此参数 addr 需要大于1M
void free_page(unsigned long addr)
{
//判断参数给定的物理地址addr的合理性, 如果物理地址addr<内存低端(1M), 则表示在内核程序或高速
//缓冲中对此不予处理. 如果addr >=系统所含物理内存最高端,则显示出错信息且内核停止工作
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
//addr验证通过, 就根据这个物理地址换算处从内存低端开始记起的内存页面号; 页面号=(addr-LOW_MEM)/4096
//可见页面号从0开始记起,此时addr中存放着页面号, 如果该页面号对应的页面映射字节!=0,则-1返回.
//此时该映射字节值=0,表示页面已释放. 如果对应页面字节原本就是0,表示该物理页面本来就是空闲的,
//说明内核代码出问题,于是显示出错信息并停机.
	addr -= LOW_MEM;
	addr >>= 12;
	if (mem_map[addr]--) return;
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 * 释放页表连续的内存块, exit需要该函数, 该函数仅处理4m长度的内存块
 */
//根据指定的线性地址和限长(页表个数),释放对应内存页表指定的内存块并置表项空闲. 页目录位于物理
//地址0开始处,共1024项,每项4字节,共占4K字节. 每个目录项指定一个页表. 内核页表从物理地址0x1000
//初开始(紧接着目录空间),共4个页表. 每个页表有1024项,每项4字节. 因此也占4k(1页)内存. 各进程
//(除了在内核代码中的进程0和1)的页表所占据的页面在进程被创建时由内核为其在主内存区申请得到.
//每个页表项对应1页物理内存, 因此一个页表最多可映射4M的物理内存. from起始线性基地址,
//size 释放的字节长度
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;
//先检测参数from给出的线性基地址是否在4M边界处. 因为该函数只能处理这种情况. 若from=0,则出错;
//说明试图释放内核和缓冲所占空间.
	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
//计算参数size给出的长度所占的页目录项数(4M的进位整数倍), 即所占页表数. 因为1各页表可管理4M
//物理内存, 所以这里用右移22位的方式把需要复制的内存长度值除以4M. 其中加上0x3fffff(4M-1)用于
//得到进位整数倍结果,即除操作若有余数则+1. 接着计算给出的线性基地址对应的起始目录项. 对应的目录
//项号=from>>22. 因为每项占4个字节, 且由于页目录表从物理地址0开始存放, 因此实际目录项指针=
//目录项号 <<2, 与上0xffc确保目录项指针范围有效, 即用于屏蔽目录项指针最后2位. 因为只移动了20位
//因此最后2位是页表项索引的内容,应屏蔽掉
	size = (size + 0x3fffff) >> 22;
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
//此时size是释放的页表个数,即页目录项数,dir是起始目录项指针.开始循环操作页目录项, 依次释放每个页
//表中的页表项,如果当前目录项无效(P位=0),表示该目录项没有使用(对应的页表不存在),则继续处理下一个
//目录项. 否则从目录项中取出页表地址pg_table, 并对该页表中的1024个表项进行处理,释放有效页表项
//(P位=1)对应的物理内存页面, 然后把该页表项清0,并继续处理下一页表项, 当一个页表所有表项都处理
//完毕就释放该页表自身占据的内存页面,并继续处理下一页目录项. 最后刷新页变换高速缓冲,并返回0
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))
			continue;
		pg_table = (unsigned long *) (0xfffff000 & *dir); //取页表地址
		for (nr=0 ; nr<1024 ; nr++) {
			if (1 & *pg_table) //若该项有效,则释放对应页
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0; //该页表项内容清0
			pg_table++; //指向页表中下一项
		}
		free_page(0xfffff000 & *dir); //释放该页表所占内存页面
		*dir = 0; //对应页表的目录项清零
	}
	invalidate(); //刷新 页变换高速缓冲
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *通过只复制内存页面来拷贝一定范围内线性地址中的内容.
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *并不复制任何内存块 - 内存块的地址需要4M的倍数(一个页目录项对应的内存长度),这样处理可使函数简单
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 * 当from=0时,说明是在为第一次fork()调用复制内核空间. 此时不想复制整个页目录项对应的内存,因为
 * 这样做会导致内存验证浪费, 只需要复制开头160个页面,对应640kB. 即使是复制这些页面也已经超出
 * 我们的需求. 但这不会占用更多的内存, 在低1M内存范围内, 不执行cow,所以这些页面可以与内核共享
 * 因此这是nr=xxxx的特殊情况(nr在程序中指页面数)
 */
//复制页目录表项和页表项; 复制指定线性地址和长度内存对应的页目录项和页表项, 从而被复制的页目录和
//页表对应的原物理内存页面区被两套页表映射而共享使用. 复制时,需申请新页面来存放新页表,原物理内存
//区将被共享. 此后两个进程(父进程和其子进程)将共享内存区, 直到有一个进程执行写操作时,内核才会为
//写操作进程分配新的内存页cow. from,to是线性地址, size是需要复制(共享)的内存长度,单位是字节
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;
//首先检测参数给出的源地址from和目的地址to的有效性. 源地址和目的地址都需要在4M内存边界地址上
//否则出错时机. 作这样的要求是因为一个页表的1024项可管理4M内存. 原地址from和目的地址to只有
//满足这个要求才能保证从一个页表的第1项开始复制页表项,且新页表的最初所有项都是有效的. 然后取得
//源地址和目的地址的起始目录项指针(from_dir, to_dir). 在根据参数给出的长度size计算要复制的
//内存块占用的页表数(即目录项数).
	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;
//得到了源起始目录项指针from_dir和目的起始目录项指针to_dir以及需要复制的页表个数size后,
//开始对每个页目录项依次申请1页内存来保存对应的页表, 且开始页表项复制操作. 如果目的目录项指定
//的页表已经存在(P=1),则出错死机. 如果源目录项无效,即指定的页表不存在(P=0),则继续循环处理下
//一个页目录项
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
			continue;
//在验证了当前源目录项和目的项正常后,取源目录项中页表地址from_page_table. 为了保存目的目录项
//对应的页表, 需要在主内存区中申请1页空闲内存页. 如果取空闲页面函数get_free_page()返回0, 则
//说明没有申请到空闲内存页面,可能是内存不够,于是返回-1退出
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
//否则设置目的目录项信息,把最后3位置位,即当前目的目录项"或上7", 表示对应页表映射的内存页面是
//用户级的,且可读可写.(如果U/S=0,则R/W就没有作用, 如果U/S=1,R/W=0, 那么运行在用户层的代码就
//只能读页面. 如果U/S=R/W=1,则就有读写权限). 然后针对当前处理的页目录项对应的页表,设置需要复制
//的页面项数, 如果是在内核空间,则仅需要复制头160页对应的页表项(nr=160),对应于开始640K物理内存.
//否则需要复制一个页表中的所有1024个页表项(nr=1024),可映射4M.
		*to_dir = ((unsigned long) to_page_table) | 7;
		nr = (from==0)?0xA0:1024;
//此时对于当前页表,开始循环赋值指定的nr个内存页面表项. 先取出源页表项内容,如果当前源页面没有
//使用, 则不用复制该表项,继续处理下一页,否则复位页表项中R/W标志,即让页表项对应的内存页面只读.
//然后将该页表项复制到目的页表中.		
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!(1 & this_page))
				continue;
			this_page &= ~2;
			*to_page_table = this_page;
//如果该页表项所指物理页面地址的地址在1M以上,则需要设置内存页面映射数组mem_map[],于是计算页面号,
//并以它为索引在页面映射数组相应项中增加引用次数. 对于位于1M以下的页面,说明是内核页面, 因此不需要
//对mem_map[]进行设置. 因为mem_map仅用于管理主内存区中的页面使用情况. 因此对于内核移动到任务0
//中且调用fork()创建任务1时(运行init()), 由于此时复制的页面还仍然都在内核代码区域, 因此以下
//判断中的语句不会执行,任务0的页面仍然可以随时读写. 只有当调用fork()的父进程代码处于主内存区(
//页面位置>1M)时才会执行, 这种情况需要在进程调用execve()并装载了新程序代码时才会出现.
//line180 语句含义时令源页表项所指内存页也为只读. 因为现在开始有两个进程共有内存区了.
//若其中1个进程需要进行写操作,则可以通过页异常写保护处理为执行写操作的进程分配1页新空闲页面,即
//进行cow操作
			if (this_page > LOW_MEM) {
				*from_page_table = this_page; //line180 令源页表项也只读
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate(); //刷新 页变换高速缓冲
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 * 将一内存页面放置(映射)到指定线性地址处, 返回页面的物理地址,如果内存不够(在访问页表或页面时)返回0
 */
//把一页物理内存页面映射到线性地址空间指定处. 把线性地址空间中指定地址address处的页面映射到
//主内存区页面page上, 主要工作是在相关页目录项和页表项中设置指定页面的信息. 如果成功则返回物理
//页面地址. 在处理缺页异常的C函数do_no_page()中会调用此函数. 对于缺页引起的异常, 由于任何缺页
//缘故而对页表作修改时,并不需要刷新cpu的页变换缓冲(即Translation lookaside buffer-TLB)
//即使页表项中标志P从0修改为1.因为无效页项不会被缓冲,因此当修改了一个无效的也比项时不需要刷新
//在此就表现为不用调用Invalidate(). page:分配的主内存区中某一页面(页帧,页框)的指针; address线性地址
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */
//这里使用了页目录jidz _pg_dir=0的条件
//先判断参数给定物理内存页面page的有效性, 如果该页面位置低于Low_mem(1M)或超出实际含有内存高端
//HIGH_MEMORY,则发出警告.LOW_MEM是主内存区可能有的最小起始位置. 当系统物理内存<=6M,主内存区
//起始于LOW_MEM处. 再查看以下该page页面是否是已经申请的页面,即判断其在内存页面映射字节图
//mem_map[]中相应字节释放已经置位,若没有则需发出警告.
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
//根据参数指定的线性地址address计算其在页目录表中对应的目录项指针,并从中取得二级页表地址.
//如果该目录项有效P=1,即指定的页表在内存中,则从中取得指定页表地址放到page_table 变量中,否则
//就申请一空闲页面给页表使用,并在对应目录项中置相应标志(7-user,u/s,r/w). 然后将该页表地址放到
//page_table变量中
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
//最后在找到的页表page_table中设置相关页表项内容,即把物理页面page的地址填入表项同时置位3个
//标志(U/S,W/R,P). 该页表项在页表中的索引值=线性地址位21-位12组成的10比特的值. 每个页表共有
//1024项(0~0x3ff).
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */ //不需要刷新 页变换高速缓冲TLB
	return page;
}
//取消写保护页面函数. 用于页异常中断过程中写保护异常的处理cow. 在内核创建进程时,新进程与父进程
//被设置成共享代码和数据内存页面,且所有这些页面均被设置成只读页面.当新进程或原进程需要向内存页面
//写数据时,cpu就会检测到这个情况并产生页面写保护异常. 于是在这个函数中内核就会首先判断要写的页面
//是否被共享. 若没有则把页面设置成可写然后退出. 若页面时共享状态,则需要重新申请一新页面并复制
//被写页面内容,以供写进程单独使用. 共享被取消.本函数供do_wp_page()调用;
//输入参数是页表项指针,是物理地址. un_wp_page: un-write protect page
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;
//取参数指定的页表项中物理页面位置(地址)并判断该页面是否是共享页面.如果原页面地址>LOW_MEM(表示
//在主内存中)且在页面映射字节图数组中值=1(表示页面仅被引用1次,页面没有被共享),则在该页面的页表项
//中置R/W标志(可写),并刷新TLB,然后返回. 如果该内存页面此时只被一个进程使用,且不是内核中的进程
//就直接把属性改为可写即可,不用再重新申请一个新页面.
	old_page = 0xfffff000 & *table_entry; //取指定页表项中物理页面地址
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
//否则就需要在主内存区申请一页空闲页面给执行写操作的进程单独使用,取消页面共享.如果原页面>内存
//低端(意味着mem_map[]>1,页面是共享的),则将原页面的页面映射字节数组值递减1, 然后将指定页表
//项内容更新为新页面地址, 并置可读写标志. 在刷新TLB后,最后将原页内容复制到新页上
	if (!(new_page=get_free_page()))
		oom(); //out of memory,内存不够处理
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *当用户视图往一共享页面上写时,该函数处理已存在的内存页面cow,通过将页面复制到一个新地址上且
 递减原页面的共享计算值实现的. 如果在代码空间,就显示段出错信息并退出
 * If it's in code space we exit with a segment error.
 */
//执行写保护页面处理. 是写共享页面处理函数. 是页异常中断处理过程中调用的C函数. 在page.s中被
//调用. error_code 是进程在写 写保护页面时由cpu自动产生,address是页面线性地址.
//写共享页面时,需复制页面cow
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
//现在还不能这样做,因为estdio库会在代码空间执行写操作
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address)) //如果地址位于代码空间,则终止执行程序.
		do_exit(SIGSEGV);
#endif
//调用un_wp_page()来处理取消页面保护. 但首先需要为其准备好参数. 参数是线性地址address指定
//页面在页表中的页表项指针,计算方法是:
//1. (address >> 10)&0xffc 计算指定线性地址中页表项在页表中的偏移地址; 根据线性地址结构, address>>12
//就是页表项中的索引, 但每项占4个字节,因此乘4后(address>>10)&0xffc就可得到页表项在表中的偏移地址
//与操作&0xffc用于限制地址范围在一个页面内. 又因为只移动了10位,因此最后2位是线性地址低12位中的
//最高2位也应该屏蔽掉. 因此求线性地址中页表项在页表中偏移地址直观一些的表示方法是:
//((address>>12)&0x3ff)<<2
//2. 0xfffff000 & *((address)>>20)&0xffc)) 用于取目录项中页表的地址值, 其中(address>>20)&0xffc
//用于取线性地址中的目录索引项在目录表中的偏移位置. 因为address>>22是目录项索引值,但每项4个字节,因此
//乘以4后: (address>>22)<<2 = address>>20 就是指定项在目录表中的偏移地址. 0xffc用于屏蔽目录项
//索引值中最后2位. 因为只移动了20位, 因此最后2位是页表索引的内容,应该屏蔽掉. 而*((address>>20)&0xffc)
//是取指定目录表项内容中对应页表的物理地址,最后与上0xfffff000用于屏蔽掉页目录项中的一些标志位
//3. 由1中页表项在页表中偏移地址加上2中目录表项内容中对应页表的物理地址即可得到页表项的指针(物理地址)
//这里对共享的页面进行复制
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}
//写页面验证; 若页面不可写.则复制页面. address是指定页面的线性地址
void write_verify(unsigned long address)
{
	unsigned long page;
//取指定线性地址对应的页目录项,根据目录项中的存在位P判断目录项对应的页表是否存在(存在位P=1?)
//若不存在P=0则返回,这样处理是因为对于不存在的页面没有共享和cow可言,且若程序对此不存在的页面执行
//写操作时,系统就会因为缺页异常而执行do_no_page(),并为这个地方使用put_page()映射一个物理页面
//接着程序从目录项中取页表地址,加上指定页面在页表中的页表项偏移值,得对应地址的页表项指针.在该表项
//中包含着给定线性地址对应的物理页面.
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
//然后判断该页表项中的位1(R/W),位0(P)标志. 如果该页面不可写(R/W=0)且存在,那么就执行共享检验
//和复制页面操作cow, 否则什么也不做,直接退出.
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}
//取得一页空闲内存页并映射到指定线性地址处; get_free_page()仅是申请取得了主内存区的一物理
//内存,而本函数则不仅是获取到一页物理内存页面,还进一步调用put_page(),将物理页面映射到指定的线性
//地址处. address是指定页面的线性地址.
void get_empty_page(unsigned long address)
{
	unsigned long tmp;
//若不能取得一空闲页面,或者不能将所取页面放置到指定地址处,则显示内存不够的信息. line279上的
//注释含义是: free_page()的参数tmp是0也没有关系, 该函数会忽略它并能正常返回.
	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */ //line279
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *在任务p中检测位于地址address处的页面,看页面是否存在,是否干净.如果是干净的话,就与当前任务共享
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable. 这里假定 p !=当前任务,且它们共享同一个执行程序
 */
//尝试对当前进程指定地址处的页面进行共享处理. 当前进程与进程p是同一执行代码,也可以认为当前进程
//是由p进程执行fork操作产生的进程, 因此它们的的代码是一样的.如果未对数据段内容做过修改那么数据
//段内容也应一样. address是进程中的逻辑地址,即当前进程欲与p进程共享页面的逻辑页面地址. 进程p是
//将被共享页面的进程. 如果p进程address处的页面存在且没有被修改过的话,就让当前进程与p进程共享之
//同时还需要验证指定的地址处是否已经申请了页面,若是则出错,死机. 返回:1页面共享处理成功; 0失败
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;
//首先分别求得指定进程p中和当前进程中逻辑地址address对应的页目录项.为了计算方便先求出指定逻辑
//地址address处的"逻辑"页目录项号,即以进程空(0~64M)算出的页目录项号. 该'逻辑'页目录项号加上进程
//p在CPU 4g线性空间中起始地址对应的页目录项,即得到进程p中地址 address 处页面所对应的4G线性空间
//中的实际页目录项from_page. 而'逻辑'页目录项号加上当前进程CPU 4G线性空间中起始地址对应的页目录项
//即可最后得到当前进程中地址address处页面所对应的4G线性空间中的实际页目录项to_page
	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);  //p进程目录项
	to_page += ((current->start_code>>20) & 0xffc); //当前进程目录项
//在得到p进程和当前进程address对应的目录项后, 下面分别对进程p和当前进程进行处理. 下面首先对p
//进程的表项进行操作,目标是取得p进程中address对应的物理内存页面地址,且该物理页面存在,而且干净
//(没有被修改过,不脏). 方法是先取目录项内容,如果该目录项无效(P=0), 表示目录项对应的二级页表不
//存在,于是返回. 否则取该目录项对应页表地址from,从而计算出逻辑地址address对应的页表项指针,并
//取出该页表项内容临时保存在phys_addr 中.
/* is there a page-directory at from? */ //from处是否存在页目录项?
	from = *(unsigned long *) from_page; //p进程目录项内容
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;  //页表指针(地址)
	from_page = from + ((address>>10) & 0xffc); //页表项指针
	phys_addr = *(unsigned long *) from_page;  //页表项内容
/* is the page clean and present? */ //物理页干净且存在吗?
//看看页表项映射的物理页面是否存在且干净, 0x41对应页表项中的D(dirty)和P标志. 如果页面不干净
//或无效则返回. 然后从该表项中取出物理页面地址再保存再phys_addr中. 最后再检查一下这个物理页面地址
//的有效性,即它不应该超过机器最大物理地址值,也不应该<内存低端(1M)
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000; //物理页面地址
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
//首先对当前进程的表项进行操作. 目标是取得当前进程中address对应的页表项地址,且该页表项还没有
//映射物理页面,即其P=0. 首先取当前进程页目录项内容到to, 如果该目录项无效(P=0),吉姆利项对应的二级
//页表不存在,则申请一空闲页面来存放页表, 并更新目录项to_page内容, 让其指向该内存页面.
	to = *(unsigned long *) to_page;  //当前进程目录项内容
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
//否则取目录项中页表地址到to, 加上页表项索引值<<2,即页表项在表中偏移地址,得到页表项地址到to_page
//针对该页表项, 如果此时检查出其对应的物理页面已经存在,即页表项的存在位P=1,则说明原本想共享进程p
//中对应的物理页面,但现在我们自己已经占有了(映射有)物理页面,于是说明内核出错,死机.
	to &= 0xfffff000; //页表地址
	to_page = to + ((address>>10) & 0xffc); //页表项地址
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
//找到了进程p中逻辑地址address处对应的干净且存在的物理页面,且也确定了当前进程中逻辑地址address
//所对应的二级页表项地址后,现在对它们进行共享处理. 首先对p进程的页表项进行修改,设置写保护(R/W=0)
//然后让当前进程复制p进程的这个页表项,此时当前进程逻辑地址address处页面即被映射到p进程逻辑地址
//address处页面映射的物理页面上
/* share them: write-protect */ //对它们进行共享处理:写保护
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();//刷新TLB,计算所操作物理页面的页面号,并将对应页面映射字节数组项中的引用递增1
	phys_addr -= LOW_MEM;
	phys_addr >>= 12; //得页面号
	mem_map[phys_addr]++;
	return 1; //返回1,表示共享处理成功
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *share_page()试图找到一个进程,可以与当前进程共享页面.参数address是当前进程数据空间中期望共享的某页面地址
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 * 首先通过检查executable->i_count 来检查是否可行,如果有其他任务已共享该inode,则它应该>1
 */
//共享页面处理; 在发生缺页异常时,首先看看能否与运行同一个执行文件的其他进程进行页面共享处理.
//该函数首先判断系统中是否有另一个进程也在运行当前进程一样的执行文件.若有,则在系统当前所有任务
//中寻找这样的任务. 若找到了这样的任务就尝试与其共享指定地址处的页面. 若系统中没有其他任务正在
//运行与当前进程相同的执行文件,那么共享页面操作的前提条件不存在,因此函数立刻退出. 判断系统中
//是否有另一个进程也在执行同一个执行文件的方法是利用进程任务数据结构中的executable字段,该字段
//指向进程正在执行程序在内存中的i节点,根据该i节点的引用次数i_count可以进行这种判断. 若executable->i_count>1
//则表明系统中可能有两个进程在运行同一个执行文件,于是可以再对任务结构数组中所有任务比较是否有
//相同的executalbe字段来最后确定多个进程运行着相同执行文件的情况. address是进程中的逻辑地址
//即当前进程欲与p进程共享页面的逻辑页面地址. 返回1共享操作成功; 0失败
static int share_page(unsigned long address)
{
	struct task_struct ** p;
//检查一下当前进程的executable字段是否指向某执行文件的i节点,以判断本进程是否有对应的执行文件
//如果没有则返回0. 如果executable的确指向某个i节点,则检查该i节点引用计数值. 如果当前进程运行
//的指向文件的内存i节点引用计数=1,则表示当前系统中只有1个进程(即当前进程)在运行该执行文件,因此
//无共享可言,直接退出函数
	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
//否则搜索任务数组中所有任务,寻找与当前进程可共享页面的进程.即运行相同执行文件的另一个进程,
//并尝试对指定地址的页面进行共享. 如果找到某个进程p,其executable字段值与当前进程的相同,
//则调用try_to_share()尝试页面共享. 若共享操作成功,则函数返回1, 否则返回0,表示共享页面操作失败		
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p) //如果该任务项空闲,则继续寻找
			continue;
		if (current == *p) //如果就是当前任务,也继续寻找
			continue;
//如果executalbe不等,表示允许的不是与当前进程相同的执行文件,因此也继续寻找			
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p)) //尝试共享页面
			return 1;
	}
	return 0;
}
//执行缺页处理, 是访问不存在页面处理函数. 页异常中断处理过程中调用的函数. 在page.s中被调用.
//error_code,address是进程在访问页面时由CPU因缺页产生异常而自动生成. error_code指出出错类型
//address是产生异常的页面线性地址; 该函数首先尝试与已加载的相同文件进行页面共享,或者只是由于
//进程动态申请内存页面而只需要映射一页物理内存页即可. 若共享操作不成功,那么只能从相应文件中读入
//所缺的数据页面到指定线性地址处
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;
//首先取线性空间中指定地址address处页面地址.从而可算出指定线性地址在进程空间中相对于进程基址
//的偏移长度值tmp,即对应的逻辑地址.
	address &= 0xfffff000;  //address处缺页页面地址
	tmp = address - current->start_code; //缺页页面对应逻辑地址
//若当前进程的executable节点指针为NULL,或指定地址超出(代码+数据)长度,则申请一页物理内存,并
//映射到指定的线性地址处. executalbe 是进程正在运行的执行文件的i节点结构. 由于任务0和任务1的
//代码在内核中,因此任务0、1以及任务1派生的没有调用过execve()的所偶任务的executable都是0.
//若该值=0或参数指定的线性地址超出代码+数据长度, 则表明进程在申请新的内存页面存放堆或栈中数据
//因此直接调用取空闲页面函数get_empty_page()为进程申请一页物理内存并映射到指定线性地址处.
//进程任务结构字段 start_code是线性地址空间中进程代码段地址,字段end_data是代码+数据长度.
//对Linux0.11它的代码段和数据段起始基址相同.
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
//否则说明所缺页面在进程执行映像文件范围内,于是就尝试共享页面操作,若成功则退出.若不成功就只能
//申请一页物理内存页面page, 然后从设备上读取执行文件中的相应页面并放置(映射)到进程页面逻辑地址tmp处
	if (share_page(tmp)) //尝试逻辑地址tmp处页面的共享
		return;
	if (!(page = get_free_page())) //申请一页物理内存
		oom();
/* remember that 1 block is used for header */ //程序头要使用1个数据块
//因为块设备上存放的执行文件映像第1块数据是程序头结构,因此在读取该文件时需要跳过第1块数据,所以
//需要首先计算缺页所在的数据块号,因为每块数据长度=BLOCK_SIZE=1K,因此一页内存可存放4个数据块.
//进程逻辑地址tmp除以数据块大小再加1即可得出缺少的页面在执行映像文件中的起始块号block. 根据这个
//块号和执行文件文件的i节点,就可以从映射位图中找到对应块设备中对应的设备逻辑块号(保存在nr[]中)
//利用bread_page()即可把这4个逻辑块读入到物理页面page中.
	block = 1 + tmp/BLOCK_SIZE;  //执行文件中起始数据块号
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block); //设备上对应的逻辑块号
	bread_page(page,current->executable->i_dev,nr); //读设备上4个逻辑块
//读设备逻辑块操作时, 可能会出现这样一种情况,即在执行文件中的读取页面位置可能离文件尾不到1个页面
//的长度,因此就可能读入一些无用的信息. 下面的操作就是把这部分超出执行文件end_data以后的部分清0	
	i = tmp + 4096 - current->end_data; //超出的字节长度值
	tmp = page + 4096; //tmp指向页面末端
	while (i-- > 0) { //页面末端i字节清0
		tmp--;
		*(char *)tmp = 0;
	}
//最后把引起缺页异常的一页物理页面映射到指定线性地址address处,若操作成功就返回.否则就释放内存页,显示内存不够	
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}
//物理内存管理初始化. 对1m以上内存区域已页面为单位进行管理前的初始化设置工作.一个页面长度为4K
//字节. 把1M以上所有物理内存划分成一个个页面,并使用一个页面映射字节数组mem_map[]来管理所有这些
//页面. 对于具有16M内存的机器,该数组共有3840项(16M-1M)/4K, 即可管理3840个物理页面. 每当一个
//物理内存页面被占用时就把mem_map[]中对应的字节值+1; 若释放一个物理页面就把对应字节值-1.若字节
//值=0, 则表示对应页面空闲; 若字节值>=1,则表示对应页面被占用或被不同程序共享占用.
//Linux0.11中,最多能管理16M的物理内存,>16M的内存将弃之不用. 对于具有16M内存的PC机系统,在没有
//设置虚拟盘RAMDISK时, start_mem通常是4M, end_mem=16M; 因此此时主内存区范围是4M~16M.共3072
//个物理页面可供分配. 范围0~1M内存空间用于内核系统(其实内核只使用0~640K,剩下的部分被高速缓冲
//和设备内存占用). start_mem是可用作页面分配的主内存区起始地址(已去除RAMDISK所占内存空间)
//end_mem是实际物理内存最大地址, 地址范围start_mem到end_mem是主内存区
void mem_init(long start_mem, long end_mem)
{
	int i;
//将1M到16M范围内所有内存页面对应的内存映射字节数组置为已占用状态,即各项字节值全部设置成USED=100
//PAGING_PAGES被定义为PAGING_MEMORY>>12, 即1M以上所有物理内存分页后的内存页面数(15M/4k=3840)
	HIGH_MEMORY = end_mem; //设置内存最高端16M
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
//计算主内存区起始内存 start_mem处页面对应内存映射字节数组中项号i和主内存区页面数. 此时mem_map[]
//的第i项正对应主内存区中第1个页面. 最后将主内存区中页面对应的数组项清0(表示空闲),对于具有16M
//物理内存的系统, mem_map[]中对应4M~16M主内存区的项被清0
	i = MAP_NR(start_mem);  //主内存区起始位置处页面号
	end_mem -= start_mem;
	end_mem >>= 12; //主内存区中总页面数
	while (end_mem-->0)
		mem_map[i++]=0; //主内存区页面对应字节值清0
}
//计算内存空闲页面数并显示
void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;
//扫描内存页面映射数组mem_map[],获取空闲页面数并显示,然后扫描所有页目录项(除0、1项).如果页目录
//项有效,则统计对应页表中有效页面数,并显示, 页目录项0~3被内核使用,因此应从第5个目录项(i=4)开始扫描
	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	for(i=2 ; i<1024 ; i++) { //初始i值应该=4
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
