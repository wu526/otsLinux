/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
// 写页面验证. 若页面不可写, 则复制页面. 定义在 mm/memory.c
extern void write_verify(unsigned long address);

long last_pid=0; // 最新进程号, 其值由 get_empty_process() 生成
// 进程空间区域写前验证函数
// 对于80386CPU, 在执行特权级0代码时不会用户空间中的页面是否是页保护的,因此在执行内核代码时
//用户空间中数据页面保护标志起不了作用, 写时复制机制也就失去了作用.verify_area()就用于此目的
//但对于80486或后来的CPU,其控制寄存器CR0中由一个写保护标志WP(位16), 内核可以通过设置该标志
//来禁止特权级0的代码向用户空间只读页面执行写数据,否则将导致发生写保护异常. 从而486以上cpu可以
//通过设置该标志来达到本函数的目的.
// 该函数对当前进程逻辑地址从 addr到addr+size这段范围以页为单位执行写操作前的检测操作.
// 由于检测判断以页为单位进行操作,因此程序首先需要找到addr所在页面开始地址start, 然后start
//加上进程数据段基地址,使这个start变换成cpu 4G线性空间中的地址. 最后循环调用 write_verify()
//对指定大小的内存空间进行写前验证. 若页面只读,则执行共享检验和复制页面操作(cow)
void verify_area(void * addr,int size)
{
	unsigned long start;
// 首先将起始地址start调整为其所在页的左边界开始位置,同时相应的调整验证区域大小.
// start & 0xfff 获得指定起始位置addr(即start)所在页面中的偏移值, 原验证范围size加上这个
//偏移值即扩展成以addr所在页面起始位置开始的范围值. 因此在line30上, 需要把验证开始位置
//start 调整成页面边界值.
	start = (unsigned long) addr;
	size += start & 0xfff;  // 
	start &= 0xfffff000;  //line30, 此时start使当前进程空间中的逻辑地址. 下面把start加上
//进程数据段在线性地址空间中的起始基地址, 变成系统整个线性空间中的地址位置. linux0.11中其数据
//段和代码段在线性地址空间中的基址和限长均相同. 然后循环进行写页面验证. 若页面不可写,则复制页面
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}
// 复制内存页表, nr: 新任务号; p: 新任务数据结构指针. 该函数为新任务在线性地址空间中设置
//代码段和数据段基址、限长,并复制页表. 由于Linux采用了cow技术,因此这里仅为新进程设置自己的
//页目录表项和页表项, 而没有实际为新进程分配物理内存页. 此时新进程与其父进程共享所有内存页面
//操作成功返回0,否则返回出错号
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;
// 取当前进程局部描述符表中代码段描述符和数据段描述符项中的段限长(字节数), 0x0f是代码段
//选择符, 0x17是数据段选择符. 然后取当前进程代码段和数据段在线性地址空间中的基地址. Linux0.11
//还不支持代码和数据段分离的情况,因此这里需要检测代码段和数据段基地址和限长是否都分别相同,
//否则内核显示出错信息,并停止运行
	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
// 设置创建中的新进程在线性地址空间中的基地址等于(64M*任务号), 并用该值设置新进程局部描述符表中
//段描述符中的基地址,接着设置新进程的页目录表项和页表项,即复制当前进程(父进程)的页目录表项
//和页表项, 此时子进程共享父进程的内存页面. 正常情况下coopy_page_tables返回0,否则表示出错,
// 则释放刚申请的页表项.
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
//复制进程. 该函数的参数是进入系统调用中断处理过程(system_call.s)开始, 直到调用本系统调用处理
//过程和调用本函数前时逐步压入栈的各寄存器的值. 这些在 system_call.s 程序中逐步压入栈的值有:
//1. cpu执行中断指令压入的用户栈地址ss和esp、eflags、返回地址cs和eip
//2. 在刚进入 system_call 时压入栈的段寄存器ds、es、fs、edx、ecx、ebx
//3. 调用 sys_call_table中sys_fork函数时压入栈的返回地址(用参数none表示)
//4. 在调用copy_process()之前压入栈的 gs,esi,edi, ebp, eax(nr)值
// nr是调用find_empty_process()分配的任务数组项号
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;
// 首先为新任务数据结构分配内存,如果内存分配出错,则返回出错码并退出.然后将新任务结构指针放入
//任务数组的nr中, nr为任务号,由前面find_empty_process()返回. 接着把当前进程任务结构复制
//到刚申请到的内存页面p开始处.
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */// 不会复制超级用户堆栈(只复制进程结构)
// 随后对复制来的进程结构进行一些修改, 作为新进程的任务结构.先将新进程的状态置位不可中断等待
//状态,以防止内核调度其执行,然后设置新进程的进程号pid和父进程号father, 并初始化进程运行时间
//片值等于其priority,接着复位新进程的信号位图、报警定时值、会话(session)领导标志leader、
//进程及其子进程在内核和用户态运行时间统计值,还设置进程开始运行的系统时间start_time
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;  // 新进程号, 由find_empty_process()得到.
	p->father = current->pid;  // 设置父进程号
	p->counter = p->priority;  // 运行时间片值
	p->signal = 0;  // 信号位图置
	p->alarm = 0;  // 报警定时值(滴答数)
	p->leader = 0;		/* process leadership doesn't inherit *///进程的领导权不能继承
	p->utime = p->stime = 0;  // 用户态时间和核心态运行时间
	p->cutime = p->cstime = 0; // 子进程用户态和核心态运行时间
	p->start_time = jiffies; // 进程开始运行时间(当前时间滴答数)
// 修改任务状态段tss数据, 由于系统给任务结构p分配了1页新内存,所以PAGE_SIZE+(long)p 让esp0
//正好指向该页顶端,ss0:esp0用作程序在内核态指向时的栈. 每个任务在GDT表中都有两个段描述符,
//一个时任务的TSS段描述符,一个是任务的LDT段描述符. line111语句就是把GDT中本任务LDT段描述符
//的选择符保存在本任务的TSS段中, 当CPU执行切换任务时,会自动从TSS中把LDT段描述符的选择符加载
//到ldtr寄存器中.
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p; // 任务内核态栈指针
	p->tss.ss0 = 0x10;  //内核态栈的段选择符(与内核数据段相同)
	p->tss.eip = eip;  // 指令代码指针
	p->tss.eflags = eflags;  // 标志寄存器
	p->tss.eax = 0;  // 这是当fork()返回时新进程会返回0的原因所在
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;  // 段寄存器仅16位有效
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);  //line111, 任务局部表描述符的选择符(LDT描述符在GDT中)
	p->tss.trace_bitmap = 0x80000000;  // 高16位有效
//如果当前任务使用了协处理器,就保存其上下文. 汇编指令clts用于清除控制寄存器CR0中的任务已
//交换(TS)标志,每当发生任务切换,cpu都会设置该标志. 该标志用于管理数学协处理器: 如果该标志
//置位,那么每个ESC指令都会被捕获(异常7). 如果协处理器存在标志MP也同时置位的话, 那么 WAIT
//指令也会捕获. 因此如果任务切换发生在一个ESC指令开始执行后,则协处理器中的内容就可能需要
//在执行新的ESC指令之前保存起来. 捕获处理句柄会保存协处理器的内容并复位TS标志. 指令fnsave
//用于把协处理器的所有状态保存到目的操作指定的内存区域中tss.i387
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
// 接下来复制进程页表. 即在线性地址空间中设置新任务代码段和数据段描述符中的基址和限长,并复制页表
//如果出错(返回值!=0), 则复位任务数组中相应项并释放为该新任务分配的用于任务结构的内存页
	if (copy_mem(nr,p)) {  // 返回值 !=0 表示出错
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
//如果父进程中有文件是打开的, 则将对应文件的打开次数+1, 因为这里创建的子进程会与父进程共享
//这些打开的文件, 将当前进程(父进程)的pwd,root,executable引用次数均+1 子进程也引用了这些i节点
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
//在GDT表中设置新任务TSS段和LDT段描述符项. 这两个段的限长均被设置成104字节.
// gdt+(nr<<1)+FIRST_TSS_ENTRY是任务nr的tss描述符项在全局表中的地址,因为每个任务占用
//GDT表中2项,因此需要包括nr<<1. 程序然后把新进程设置成就绪,另外在任务切换时,任务寄存器tr
//由CPU自动加载.最后返回新进程号		
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case *///最后将新任务置成就绪状态,以防万一
	return last_pid;
}
//为新进程取得不重复的进程号last_pid, 函数返回在任务数组中的任务号(数组项)
int find_empty_process(void)
{
	int i;
// 首先获取新的进程号. 如果last_pid增1后超出进程号的正数表示范围,则重新从1开始使用pid号.
//然后再任务数组中搜索刚设置的pid号是否以及被任何任务使用.如果是则跳转到函数开始处重新获得
//一个pid号,接着在任务数组中为新任务寻找一个空闲项,并返回项号. last_pid是一个全局变量,
//不用返回, 如果此时任务数组中64各项已经被全部占用,则返回出错码
	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
