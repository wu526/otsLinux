/*
 * malloc.c --- a general purpose kernel memory allocator for Linux.
 *  linux的通用内核内存分配函数
 * Written by Theodore Ts'o (tytso@mit.edu), 11/29/91
 *该函数被编写成尽可能的快, 从而可以从中断层调用此函数; 限制: 使用该函数一次所能分配的最大内存是4k, 即Linux中内存页面的大小
 * This routine is written to be as fast as possible, so that it
 * can be called from the interrupt level.
 *
 * Limitations: maximum size of memory we can allocate using this routine
 *	is 4k, the size of a page in Linux.
 *
 * The general game plan is that each page (called a bucket) will only hold
 * objects of a given size.  When all of the object on a page are released,
 * the page can be returned to the general free pool.  When malloc() is
 * called, it looks for the smallest bucket size which will fulfill its
 * request, and allocate a piece of memory from that bucket pool.
 *
 * Each bucket has as its control block a bucket descriptor which keeps 
 * track of how many objects are in use on that page, and the free list
 * for that page.  Like the buckets themselves, bucket descriptors are
 * stored on pages requested from get_free_page().  However, unlike buckets,
 * pages devoted to bucket descriptor pages are never released back to the
 * system.  Fortunately, a system should probably only need 1 or 2 bucket
 * descriptor pages, since a page can hold 256 bucket descriptors (which
 * corresponds to 1 megabyte worth of bucket pages.)  If the kernel is using 
 * that much allocated memory, it's probably doing something wrong.  :-)
 *
 * Note: malloc() and free() both call get_free_page() and free_page()
 *	in sections of code where interrupts are turned off, to allow
 *	malloc() and free() to be safely called from an interrupt routine.
 *	(We will probably need this functionality when networking code,
 *	particularily things like NFS, is added to Linux.)  However, this
 *	presumes that get_free_page() and free_page() are interrupt-level
 *	safe, which they may not be once paging is added.  If this is the
 *	case, we will need to modify malloc() to keep a few unused pages
 *	"pre-allocated" so that it can safely draw upon those pages if
 * 	it is called from an interrupt routine.
 *
 * 	Another concern is that get_free_page() should not sleep; if it 
 *	does, the code is carefully ordered so as to avoid any race 
 *	conditions.  The catch is that if malloc() is called re-entrantly, 
 *	there is a chance that unecessary pages will be grabbed from the 
 *	system.  Except for the pages for the bucket descriptor page, the 
 *	extra pages will eventually get released back to the system, though,
 *	so it isn't all that bad.
 */
/*编写该函数所遵循的一般规则是:每页(被称为一个存储桶)仅分配所要容纳对象的大小.当一页上所有的对象都释放后,该页就可以返回通用空闲内存池
当malloc()被调用时,会寻找满足要求的最小的存储桶,并从该存储桶中分配一块内存; 每个存储桶都有一个作为其控制用的存储桶描述符,其中记录了页面上
有多少对象正被使用以及该页上空闲内存的列表. 就像存储桶自身一样,存储桶描述符也是存储在使用get_free_page()申请到的页面上的, 但与存储桶
不同的是,桶描述符所占用的页面讲不会再释放给系统. 幸运的是一个系统大约只需要1到2页的桶描述符页面,因为一个页面可以存放256个桶描述符(对应
1M内存的存储桶页面). 如果系统为桶描述符分配了许多内存,那肯定系统什么地方出问题了.

malloc()和free()都关闭了中断的代码部分调用了get_free_page()和free_page(), 以使malloc(), free()可以安全的被从中断程序中调用
(当网络代码,尤其是NFS等被加入到Linux中时就可能需要这种功能). 但前提是假设get_free_page, free_page()是可以安全地在中断级程序中
使用的. 这在一旦加入了分页处理之后就可能不是安全的. 如果真是这种情况,那么就需要修改malloc()来预先分配几页不用的内存, 如果malloc(),
free()被从中断程序中调用时就可以安全得使用这些页面;

get_free_page()不应该睡眠,如果会睡眠的话,则为了防止任何竞争条件, 代码需要仔细地安排顺序, 关键在于如果malloc()是可以重入地被调用的话
就会存在不必要的页面被从系统中取走的机会, 除了用于桶描述符的页面,这些额外的页面最终会释放给系统,所以并不是象想象的那样不好
*/
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>
//存储桶描述符结构
struct bucket_desc {	/* 16 bytes */
	void			*page; //该桶描述符对应的内存页面指针
	struct bucket_desc	*next; //下一个描述符指针
	void			*freeptr; //指向本桶中空闲内存位置的指针
	unsigned short		refcnt; //引用计数
	unsigned short		bucket_size; //本描述符对应存储桶的大小
};
//存储桶描述符目录结构
struct _bucket_dir {	/* 8 bytes */
	int			size; //该存储桶的大小(字节数)
	struct bucket_desc	*chain; // 该存储桶目录项的桶描述符链表指针
};

/*
 * The following is the where we store a pointer to the first bucket
 * descriptor for a given size.  
 * 存放第一个给定大小存储桶描述符指针的地方
 * If it turns out that the Linux kernel allocates a lot of objects of a
 * specific size, then we may want to add that specific size to this list,
 * since that will allow the memory to be allocated more efficiently.
 * However, since an entire page must be dedicated to each specific size
 * on this list, some amount of temperance must be exercised here.
 *
 * Note that this list *must* be kept in order.
 * 如果linux内核分配了许多指定大小的对象,那么就希望将该指定的大小加到该列表中(链表), 因为这样就可以使内存的分配更有效,但因为一页完整内存
 * 页面必须用于列表中指定大小的所有对象,所以需要做总数方面的测试操作
 */
//存储桶目录列表(数组)
struct _bucket_dir bucket_dir[] = {
	{ 16,	(struct bucket_desc *) 0}, //16字节长度的内存块
	{ 32,	(struct bucket_desc *) 0},//32字节长度的内存块
	{ 64,	(struct bucket_desc *) 0},//64字节长度的内存块
	{ 128,	(struct bucket_desc *) 0},//128字节长度的内存块
	{ 256,	(struct bucket_desc *) 0},//256字节长度的内存块
	{ 512,	(struct bucket_desc *) 0},//512字节长度的内存块
	{ 1024,	(struct bucket_desc *) 0},//1024字节长度的内存块
	{ 2048, (struct bucket_desc *) 0},//2048字节长度的内存块
	{ 4096, (struct bucket_desc *) 0},//4096字节长度的内存块
	{ 0,    (struct bucket_desc *) 0}};   /* End of list marker */

/*
 * This contains a linked list of free bucket descriptor blocks
 */
struct bucket_desc *free_bucket_desc = (struct bucket_desc *) 0; //含有空闲桶描述符内存块的链表

/*
 * This routine initializes a bucket description page. //初始化一页桶描述符页面
 */
//初始化桶描述符; 建立空闲桶描述符链表, 并让free_bucket_desc指向第一个空闲桶描述符
static inline void init_bucket_desc()
{
	struct bucket_desc *bdesc, *first;
	int	i;
//申请一页内存,用于存放桶描述符,如果失败,则显示初始化桶描述符时内存不够 出错信息,死机	
	first = bdesc = (struct bucket_desc *) get_free_page();
	if (!bdesc)
		panic("Out of memory in init_bucket_desc()");
//计算一页内存中可存放的桶描述符数量,然后对其建立单向连接指针		
	for (i = PAGE_SIZE/sizeof(struct bucket_desc); i > 1; i--) {
		bdesc->next = bdesc+1;
		bdesc++;
	}
	/*
	 * This is done last, to avoid race conditions in case 
	 * get_free_page() sleeps and this routine gets called again....
	 */
//在最后处理的,目的是为了避免在get_free_page()睡眠时该子程序又被调用而引起的竞争条件; 将空闲桶描述符符指针free_bucket_desc加入链表中
	bdesc->next = free_bucket_desc;
	free_bucket_desc = first;
}
//分配动态内存函数; len请求的内存块长度; 返回: 指向被分配内存的指针; 如果失败则返回NULL
void *malloc(unsigned int len)
{
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc;
	void			*retval;

	/*
	 * First we search the bucket_dir to find the right bucket change
	 * for this request. 首先搜索存储桶目录bucket_dir 来寻找合适请求的桶大小
	 */
//搜索存储桶目录,寻找适合申请内存块大小的桶描述符链表, 如果目录项的桶字节数 > 请求的字节数, 就找到了对应的桶目录项.	
	for (bdir = bucket_dir; bdir->size; bdir++)
		if (bdir->size >= len)
			break;
//搜索完整个目录都没有找到合适大小的目录项,则表明所请求的内存块太大,超出了该程序的分配显示(最长为1个页面), 于是显示出错信息,死机
	if (!bdir->size) {
		printk("malloc called with impossibly large argument (%d)\n",
			len);
		panic("malloc: bad arg");
	}
	/*
	 * Now we search for a bucket descriptor which has free space 现在来搜索具有空闲空间的桶描述符
	 */
	cli();	/* Avoid race conditions */ //为了避免出现竞争条件,先关中断
//搜索对应桶目录项中描述符链表,查找具有空闲空间的桶描述符,如果桶描述符的空闲内存指针freeptr不为空,则表示找到了相应的桶描述符
	for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) 
		if (bdesc->freeptr)
			break;
	/*
	 * If we didn't find a bucket with free space, then we'll 如果没有找到具有空闲空间的桶描述符,那么就要新建立一个该目录
	 * allocate a new one. 项的描述符.
	 */
	if (!bdesc) {
		char		*cp;
		int		i;
//若free_bucket_desc还空,表示第一次调用该程序, 则对描述符链表进行初始化; free_bucket_desc 指向第一个空闲桶描述符
		if (!free_bucket_desc)	
			init_bucket_desc();
//取free_bucket_desc指向的空闲桶描述符,并让free_bucket_desc指向下一个空闲桶描述符			
		bdesc = free_bucket_desc;
		free_bucket_desc = bdesc->next;
//初始化该新的桶描述符. 令其引用数量=0, 桶的大小等于对应桶目录的大小; 申请一页内存页面,让描述符的页面指针page指向该页面; 空闲内存指针
//也指向该页开头,因为此时全为空闲;
		bdesc->refcnt = 0;
		bdesc->bucket_size = bdir->size;
		bdesc->page = bdesc->freeptr = (void *) cp = get_free_page();
//如果申请页面内存操作失败,则显示错误, 死机
		if (!cp)
			panic("Out of memory in kernel malloc()");
		/* Set up the chain of free objects */
//在该页空闲内存中建立空闲对象链表; 以该桶目录项指定的桶大小为对象长度, 对该页内存进行划分,并使每个对象的开始4字节设置成指向下一个对象的指针
		for (i=PAGE_SIZE/bdir->size; i > 1; i--) {
			*((char **) cp) = cp + bdir->size;
			cp += bdir->size;
		}
//最后一个对象开始处的指针设置为0(NULL); 然后让该桶描述符的下一描述符指针字段指向对应桶目录项指针chain所指的描述符, 而桶目录的chain
//指向该桶描述符,即将该描述符插入到描述符链 链头处
		*((char **) cp) = 0;
		bdesc->next = bdir->chain; /* OK, link it in! */
		bdir->chain = bdesc;
	}
//返回指针即等于该描述符对应页面的当前空闲指针,然后调整该空闲空间指针指向下一个空闲对象,并使描述符中对应页面中对象引用计数+1	
	retval = (void *) bdesc->freeptr;
	bdesc->freeptr = *((void **) retval);
	bdesc->refcnt++;
	sti();	/* OK, we're safe again */ //打开中断, 并返回指向空闲内存对象的指针
	return(retval);
}

/*
 * Here is the free routine.  If you know the size of the object that you
 * are freeing, then free_s() will use that information to speed up the
 * search for the bucket descriptor.
 * 如果知道释放对象的大小,则free_s()将使用该信息加速搜索对应桶描述符的速度
 * We will #define a macro so that "free(x)" is becomes "free_s(x, 0)"
 * 将定义一个宏, 使得 free(x) 变为 free_s(x, 0);
 */
//释放存储桶对象; obj: 对应对象指针; size大小
void free_s(void *obj, int size)
{
	void		*page;
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc, *prev;
//计算该对象所在的页面
	/* Calculate what page this object lives in */
	page = (void *)  ((unsigned long) obj & 0xfffff000);
	/* Now search the buckets looking for that page */ //现在搜索存储桶目录项所链接的桶描述符,寻找该页面
	for (bdir = bucket_dir; bdir->size; bdir++) {
		prev = 0;
		/* If size is zero then this conditional is always false */ //如果参数size=0, 则下面条件肯定是false
		if (bdir->size < size)
			continue;
//搜索对应目录项中链接的所有描述符, 查找对应页面. 如果某描述符页面指针=page则表示找到了相应的描述符,跳转到found,如果描述符不含有对应
//page,则让描述符指针prev指向该描述符			
		for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) {
			if (bdesc->page == page) 
				goto found;
			prev = bdesc;
		}
	}
//若搜索了对应目录项的所有描述符都没有找到指定的页面,则显示出错信息,死机	
	panic("Bad address passed to kernel free_s()");
found:
//找到对应的桶描述符后, 首先关闭中断, 然后将该对象内存块链入空闲块对像链表中,并使该描述符的对象引用计数-1
	cli(); /* To avoid race conditions */ //为了避免竞争条件, 关闭中断
	*((void **)obj) = bdesc->freeptr;
	bdesc->freeptr = obj;
	bdesc->refcnt--;
	if (bdesc->refcnt == 0) { //如果引用计数已=0, 就可以释放对应的内存页面和该桶描述符
		/*
		 * We need to make sure that prev is still accurate.  It
		 * may not be, if someone rudely interrupted us.... 需要确信prev仍然是正确的,若某新恒星粗鲁地中断了我们, 就有可能不是了
		 */
//如果prev已经不是搜索到的描述符的前一个描述符,则重新搜索当前描述符的前一个描述符.		
		if ((prev && (prev->next != bdesc)) ||
		    (!prev && (bdir->chain != bdesc)))
			for (prev = bdir->chain; prev; prev = prev->next)
				if (prev->next == bdesc)
					break;
		if (prev) //如果找到了该前一个描述符,则从描述符链中删除当前描述符
			prev->next = bdesc->next;
		else { //如果prev=NULL,则说明当前一个描述符是该目录项首个描述符, 即目录项中chain应该直接指向当前描述符bdesc,否则表示链表
//有问题,则显示出错并死机; 因此为了将当前描述符从链表中删除,应该让chain指向下一个描述符		
			if (bdir->chain != bdesc)
				panic("malloc bucket chains corrupted");
			bdir->chain = bdesc->next;
		}
//释放当前描述符所操作的内存页面,并将该描述符插入空闲描述符链表开始处
		free_page((unsigned long) bdesc->page);
		bdesc->next = free_bucket_desc;
		free_bucket_desc = bdesc;
	}
	sti(); //开中断,返回
	return;
}

