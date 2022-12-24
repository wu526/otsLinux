//移动到用户模式运行; 该函数利用 iret 指令实现从内核模式移动到初始任务0中去执行
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \  //保存堆栈指针esp到eax寄存器中
	"pushl $0x17\n\t" \ //首先将堆栈段选择符(SS)入栈
	"pushl %%eax\n\t" \ //然后将保存的堆栈指针值(esp)入栈
	"pushfl\n\t" \ //将标志寄存器(eflags)内容入栈
	"pushl $0x0f\n\t" \ //将Task0代码段选择符cs入栈
	"pushl $1f\n\t" \ // 将下面标号1的的偏移地址eip入栈
	"iret\n" \ //执行中断返回指令,则会跳转到下面标号1处
	"1:\tmovl $0x17,%%eax\n\t" \ //此时开始执行任务0
	"movw %%ax,%%ds\n\t" \ //初始化段寄存器指向本局部表的数据段
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

#define sti() __asm__ ("sti"::) //开启中断嵌入汇编宏函数
#define cli() __asm__ ("cli"::) //关中断
#define nop() __asm__ ("nop"::) //空操作

#define iret() __asm__ ("iret"::)  //中断返回
//设置门描述符宏; 根据参数中的中断或异常处理过程地址addr,门描述符类型type和特权级信息dpl, 设置位于地址gate_addr处的门描述符;
//注意: 下面"偏移"值是相对于内核代码或数据段来说的). gate_addr 描述符地址; type:描述符类型域值; dpl描述符特权级; addr偏移地址
//%0 (由dpl,type组合成的类型标志字); %1 描述符低4字节地址; %2 描述符高4字节地址; %3 edx 程序偏移地址addr; %4 eax(高字中含有段选择符0x8)
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \ //将偏移地址低字与选择符组合成描述符低4字节(eax)
	"movw %0,%%dx\n\t" \ //将类型标志字与偏移高字组合成描述符高4字节(edx)
	"movl %%eax,%1\n\t" \ //分别设置门描述符的低4字节和高4字节
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))
//设置中断门函数(自动屏蔽随后的中断); n中断号, addr中断程序偏移地址; &idt[n]是中断描述符表中中断号n对应项的偏移值; 中断描述符的类型是14
//特权级是0
#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)
//设置陷阱门函数; n中断号; addr中断程序偏移地址; &idt[n]是中断描述符表中中断号n对应项的偏移值; 中断描述符的类型是15,特权级是0
#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)
//设置系统陷阱门函数; set_trap_gate()设置的描述符的特权级=0,这里是3. 因此set_system_gate()设置的中断处理过程能够被所有程序执行
//如单步调试、溢出出错和边界超出出错处理. 参数n中断号; addr中断程序偏移地址; &idt[n] 是中断描述符表中中断号n对应项的偏移值; 中断描述符
//的类型是15,特权级是3
#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)
//设置段描述符函数(内核中没有用到); gate_addr 描述符地址; type 描述符中类型域值; dpl 描述符特权层值; base段的基地址; limit段限长
//这里赋值对象弄反了, line43应该是*((gate_addr)+1), line49才是 *(gate_addr)
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \  //line43 描述符低4字节
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \ //line49 描述符高4字节
		((limit) & 0x0ffff); }
//在全局表中设置任务状态段/局部表描述符. 状态段和局部表段的长度均被设置成104字节; n在全局表中描述符项n所对应的地址; addr状态段/局部表所在
//内存的基地址; type描述符中的标志类型字节. %0 eax(地址addr); %1 (描述符项n的地址); %2 描述符项n的地址偏移2处; %3描述符项n的地址偏移4处
//%4描述符项n的地址偏移5处; %5 描述符项n的地址偏移6处; %6 描述符项n的地址偏移7处
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \ //将tss(或ldt)长度放入描述符长度域(第0~1字节)
	"movw %%ax,%2\n\t" \ //将基地址的低字放入描述符第2~3字节
	"rorl $16,%%eax\n\t" \ //将基地址高字右循环移入ax中(低字进入高字处)
	"movb %%al,%3\n\t" \ // 将基地址高字中低字节移入描述符第4字节
	"movb $" type ",%4\n\t" \ // 将标志类型字节移入描述符的第5字节
	"movb $0x00,%5\n\t" \ //描述符的第6字节置0
	"movb %%ah,%6\n\t" \ //将基地址高字中高字节移入描述符第7字节
	"rorl $16,%%eax" \ //再右循环16bit, eax恢复原值
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)
//在全局表中设置任务状态段描述符; n该描述符的指针; addr描述符项中段的基地址值; 任务状态段描述符的类型是0x89
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
//在全局表中设置局部表描述符; n该描述符的指针; addr描述符项中段的基地址; 局部表段描述符的类型是0x82
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
