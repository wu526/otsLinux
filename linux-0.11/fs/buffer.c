/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have 用于实现缓冲区高速缓存功能. 通过不让中断处理过程
 * been avoided by NEVER letting a interrupt change a buffer (except for the 改变缓冲区,而是让调用者来执行,避免竞争条件
 * data, of course), but instead letting the caller do it. NOTE! As interrupts (改变数据除外). 由于中断可以唤醒一个调用
 * can wake up a caller, some cli-sti sequences are needed to check for 者, 因此就需要开关中断指令序列来检测由于调用而睡眠
 * sleep-on-calls. These should be extremely quick, though (I hope). 但需要非常地快
 */

/*
 * NOTE! There is one discordant note here: checking floppies for  有一个程序应不属于这里: 检测软盘是否更换, 但这里是放置
 * disk change. This is where it fits best, I think, as it should 该程序最好的地方了,因为它需要使已更换软盘缓冲失效
 * invalidate changed floppy-disk-caches.
 */

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
//变量end由编译时的链接程序ld生成,用于表明内核代码的末端.即指明内核模块末端位置. 也可以从编译内核时生成的sysmtemp.map文件中查出. 这里
//用它来表明高速缓冲区开始于内核代码末端位置. buffer_wait变量是等待空闲缓冲块而睡眠的任务队列头指针. 与缓冲区块头部结构中b_wait指针的
//作用不同. 当任务申请一个缓冲块而正好遇到系统缺乏可用空闲缓冲块时, 当前任务就好被添加到buffer_wait 睡眠等待队列中. b_wait 则是专门
//供等待指定缓冲块(即b_wait对应的缓冲块)的任务使用的等待队列头指针.
extern int end;
struct buffer_head * start_buffer = (struct buffer_head *) &end;
struct buffer_head * hash_table[NR_HASH];  // NR_HASH=307
static struct buffer_head * free_list;  // 空闲缓冲块链表头指针
static struct task_struct * buffer_wait = NULL;  // 等待空闲缓冲块而睡眠的任务队列
// 定义系统缓冲区中含有的缓冲块个数. NR_BUFFERS是一个定义在linux/fs.h中的宏, 其值即是变量nr_buffers,且在fs.h中被声明为全局变量
//大写通常都是一个宏名称, linus这样编写是为了利用大写来隐含的表示nr_buffers是一个在内核初始化之后不再改变的常量. 将在后面的缓冲区
int NR_BUFFERS = 0;  //初始化函数buffer_init()中被设置
//等待指定缓冲块解锁; 如果指定的缓冲块bh已经被上锁就让进程不可中断地睡眠在该缓冲块的等待队列b_wait中.在缓冲块解锁时,其等待队列上的所有
//进程将被唤醒, 虽然是在关闭中断之后睡眠的,这样做并不会影响在其他进程上下文中响应中断.因为每个进程都在自己的TSS段中保存了标志寄存器的值
//所以在进程切换时cpu中当前eflags的值也随之改变. 使用sleep_on()进入睡眠状态的进程需要用wake_up()明确的唤醒.
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock) //如果已被上锁则进程进入睡眠,等待其解锁
		sleep_on(&bh->b_wait);
	sti();
}
//设备数据同步; 同步设备和内存高速缓冲中数据;
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;
//首先调用i节点同步函数,把内存i节表点中所有修改过的i节点写入高速缓冲中.然后扫描所有高速缓冲区,对已被修改的缓冲块产生写盘请求,将缓冲中数据
//写入盘中,做到高速缓冲中的数据与设备中的同步
	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;  //bh指向缓冲区开始处
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);  //等待缓冲区解锁(如果已上锁的话)
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);  // 产生写块设备请求
	}
	return 0;
}
//对指定设备进行高速缓冲数据与设备上数据的同步操作; 首先搜索高速缓冲区中所有缓冲块,对于指定设备dev的缓冲块,若其数据已被修改过就写入盘中
//(同步操作), 然后把内存中i节点表数据写入高速缓冲中, 之后再对指定设备dev执行一次与上述相同的写盘操作
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;
//首先对参数指定的设备执行数据同步操作,让设备上的数据与高速缓冲区中的数据同步; 方法是: 扫描高速缓冲区中所有缓冲块,对指定设备dev的缓冲块
//先检测其是否已被上锁,若已上锁就睡眠等待其解锁, 然后再判断一次该缓冲块是否还是指定设备的缓冲块并且已被修改过(b_dirt置位), 若是就对其
//执行写盘操作, 因为在睡眠期间该缓冲块有可能已被释放或被挪作它用,所以在继续执行前需要再次判断一些该缓冲块是否还是指定设备的缓冲块
	bh = start_buffer;  // bh指向缓冲区开始处
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)  // 不是设备dev的缓冲块则继续
			continue;
		wait_on_buffer(bh);  // 等待缓冲区解锁(如果已上锁的话)
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
// 在将i节点数据写入高速缓冲,让i节点表inode_table中的inode与缓冲中的信息同步	
	sync_inodes();
//然后在高速缓冲中的数据更新后,再把它们与设备中的数据同步.这里采用两遍同步操作是为了提高内核执行效率.第一遍缓冲区同步操作可以让内核中许多
//脏块变干净,使得i节点的同步操作能够高效执行. 本次缓冲区同步操作则是把那些由于i节点同步操作而又变脏的缓冲块与设备中数据同步
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}
//使指定设备在高速缓冲区中的数据无效; 扫描高速缓冲区中所有缓冲块. 对指定设备的缓冲块复位其有效(更新)标志和已修改标志
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)  //如果不是指定设备的缓冲块,则继续扫描下一块
			continue;
		wait_on_buffer(bh); //等待该缓冲区解锁(如果已被上锁)
		if (bh->b_dev == dev) //由于进程执行过睡眠等待,所以需要再判断以下缓冲区是否是指定设备的
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and  该子程序检查一个软盘是否已被更换,如果已更换就使高速缓冲中与
 * invalidates all buffer-cache-entries in that case. This 该软盘对应的所有缓冲区无效, 该子程序相对来说比较慢,要尽量少使用
 * is a relatively slow routine, so we have to try to minimize using 所以仅在mount、open时才调用. 这是将速度和实用性相
 * it. Thus it is called only upon a 'mount' or 'open'. This 结合的最好方法. 若在执行过程中更换软盘, 就会导致数据的丢失
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is 尽管目前该子程序仅用于软盘,以后任何可移动介质的块设备
 * that any additional removable block-device will use this routine,  都将使用该程序, mount/open 操作不需要知道是软盘
 * and that mount/open needn't know that floppies/whatever are  还是其他什么特殊介质
 * special.
 */
void check_disk_change(int dev)  // 检查磁盘是否更换, 如果已更换就使对应高速缓冲区无效
{
	int i;
//首先检测一下是不是软盘设备. 因为现在仅支持软盘可移动介质. 如果不是则退出. 然后测试软盘是否已更换. 如果没有则退出.
	if (MAJOR(dev) != 2)
		return;
	if (!floppy_change(dev & 0x03))
		return;
	for (i=0 ; i<NR_SUPER ; i++) //软盘已更换,所以释放对应设备的i节点位图和逻辑块位图所占的高速缓冲区; 并让该设备的i节点和数据
		if (super_block[i].s_dev == dev) //块新所占据的高速缓冲块无效.
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);
}
//hash散列函数定义和hash表项的计算宏. hash表的主要作用是减少查找比较元素所花费的时间.通过在元素的存储位置与关键字之间建立一个对应关系
//就可以直接通过函数计算立刻查询到指定的元素. 建立hash函数的指导条件主要是尽量确保散列到任何数组项的概率基本相等.建立hansh函数的方法
//有多种, linux.011主要采用了关键字除留余数法. 因为寻找缓冲块有两个条件,即设备号dev和缓冲块号block, 因此设计的hash函数肯定需要包含
//这两个关键值. 这里两个关键字的异或操作只是计算关键字的一种方法. 在对关键字进行MOD运算就可以保证函数所计算得到的值都处于函数数组项范围内
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]
//从hash队列和空闲缓冲队列中移走缓冲块; hash队列是双向链表结构,空闲缓冲块队列是双向循环链表结构
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	if (hash(bh->b_dev,bh->b_blocknr) == bh)  //如果该缓冲区是该队列的头一个块,则让hash表的对应项指向本队列中的下一个缓冲区
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	if (free_list == bh) //如果空闲链表头指向本缓冲区,则让其指向下一个缓冲区
		free_list = bh->b_next_free;
}
//将缓冲块插入空闲链表尾部,同时放入hash队列中
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
//如果该缓冲块对应一个设备,将其插入新hash队列中; 当hash表某项第1次插入项时, hash()计算值=NULL, 因此line161上得到的bh->b_next
//肯定是NULL, 所以line163 应该在bh->next 不为NULL时才能给b_prev 赋bh值, 即line163前应该增加判断 if(bh->b_next)
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);  //line161
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;  // line163 此句前应该添加 if(bh->b_next)判断
}
//利用hash表在高速缓冲中寻找给定设备和指定块号的缓冲区块; 找到则返回缓冲区块的指针,否则返回NULL
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;
//搜索hahs表,寻找指定设备号和块号的缓冲块
	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
struct buffer_head * get_hash_table(int dev, int block)  //利用hash表在高速缓冲区中寻找指定的缓冲块.若找到则对该缓冲块上锁并返回块头指针
{
	struct buffer_head * bh;

	for (;;) { //在高速缓冲中寻找给定设备和指定块的缓冲区块,如果没有找到则返回NULL,退出
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		bh->b_count++; // 对该缓冲块增加引用计数,并等待该缓冲块解锁. 由于经过了睡眠状态,因此有必要再验证该缓冲块的正确性,并返回缓冲块头指针
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		bh->b_count--;//如果在睡眠是该缓冲块所属的设备号或块号发生了改变,则撤销对它的引用计数,重新寻找
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock) //宏用于同时判断缓冲区的修改标志和锁定标志,且定义修改标志的权重比锁定标志大
//取高速缓冲中指定的缓冲块; 检测指定(设备号和块号)的缓冲区是否已经在高速缓冲中. 如果指定块已经在高速缓冲中,则返回对应缓冲区头指针退出.
//如果不在,就需要在高速缓冲中设置一个对应设备号和块号的新项,返回相应缓冲区头指针.
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;

repeat: //搜索hash表,如果指定块已经在高速缓冲中,则返回对应缓冲区头指针,退出
	if (bh = get_hash_table(dev,block))
		return bh;
	tmp = free_list; //扫描空闲数据块链表,寻找空闲缓冲区. 先让tmp指向空闲链表的第一个空闲缓冲区头
	do { //如果该缓冲区正被使用,则继续扫描下一项,对于b_count=0的块,即高速缓冲中当前没有引用的块不一定就是干净的或没有锁定的.因此
//还是需要继续下面的判断和选择. 如当一个任务改写过一块内容后就释放了,于是该块b_count=0,b_lock不等于0; 当一个任务执行breada()预读
//几个块时,只要ll_rw_block()命令发出后,就会递减b_count, 但此时实际上硬盘访问操作可能还在进行, 因此此时b_lock=1,但b_count=0
		if (tmp->b_count)
			continue;
//缓冲头指针bh=空,或者tmp所指缓冲头的标志权重小于bh头标志的权重,则让bh指向tmp缓冲块头, 如果该tmp缓冲块头表明缓冲块既没有修改也没有
//锁定标志置位,则说明已为指定设备上的块取得对应的高速缓冲块,则退出循环, 否则就继续执行本循环,看看能否找到一个BADNESS()最小的缓冲块			
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
//如果循环检查发现所有缓冲块都正在被使用中(所有缓冲块的头部引用计数都>0), 则睡眠等待有空闲缓冲块可用. 当有空闲缓冲块可用时本进程会被
//明确地唤醒,然后就跳转到函数开始处重新查找空闲缓冲块
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
//执行到这里,说明已找到一个比较适合的空闲缓冲块了,于是先等待该缓冲区解锁, 如果在睡眠阶段该缓冲区又被其他任务使用的话,只好重复上述寻找过程	
	wait_on_buffer(bh);
	if (bh->b_count)  // 又被占用
		goto repeat;
//如果该缓冲区已被修改,则将数据写盘,并再次等待缓冲区解锁, 同样地,若该缓冲区又被其他任务使用的话, 只好再重复上述寻找过程
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count) //又被占用
			goto repeat;
	}
/* NOTE!! While we slept waiting for this block, somebody else might */ //当进程为了等待该缓冲块而睡眠时,其他进程可能已经将
/* already have added "this" block to the cache. check it */ //该缓冲块加入高速缓冲中,所以也要对此进行检查
//在高速缓冲hash表中检查指定设备和块的缓冲块是否乘睡眠之际已经被加入进去,如果是的话就再次重复上述寻找过程
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	bh->b_count=1;  // 占用此缓冲块,置引用计数=1, 复位修改标志和有效(更新)标志
	bh->b_dirt=0;
	bh->b_uptodate=0;
	remove_from_queues(bh);  // 从hash队列和空闲块链表中移除该缓冲区头,让该缓冲区用于指定设备和其上的指定块.然后根据此新的设备号
	bh->b_dev=dev; //和块号重新插入空闲链表和hash队列新位置处,并最终返回缓冲头指针
	bh->b_blocknr=block;
	insert_into_queues(bh);
	return bh;
}
//释放指定缓冲块; 等待该缓冲块解锁; 然后引用计数递减1,并明确地唤醒等待空闲缓冲块的进程.
void brelse(struct buffer_head * buf)
{
	if (!buf)  //如果缓冲头指针无效则返回
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
//从设备上读取数据块; 该函数根据指定的设备号dev和数据块号block, 首先在高速缓冲区中申请一块缓冲块.如果该缓冲块中已经包含有效的数据就
//直接返回该缓冲块指针,否则从设备中读取指定的数据块到该缓冲块中并返回缓冲块指针
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;
//在高速缓冲区中申请一块缓冲块. 如果返回值=NULL,则表示内核出错, 停机; 然后判断其中是否已有可用数据.如果该缓冲块中数据是有效的
//可以直接使用, 则返回
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
	if (bh->b_uptodate)
		return bh;
//否则就调用底层块设备读写ll_rw_block(), 产生读设备块请求.然后等待指定数据块被读入.并等待缓冲区解锁. 在睡眠醒来后,如果该缓冲区已更新
//则返回缓冲区头指针,退出,否则表明读设备操作失败,于是释放该缓冲区,返回NULL,退出
	ll_rw_block(READ,bh);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return NULL;
}
//复制内存块,从from地址复制一块(1024字节)数据到to位置
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:"cx","di","si")

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
//读设备上一个页面(4个缓冲块)的内容到指定内存地址处 address保存页面数据的地址; dev指定设备号; b[4]含有4个设备数据块号的数组;
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;
//循环执行4次,根据放在数据b[]中的4个块号从设备dev中读取一页内容放到指定内存位置address处. 对于参数b[i]给出的有效块号,函数首先从高速
//缓冲中取指定设备和块号的缓冲块. 如果缓冲块中数据无效(未更新)则产生读设备请求从设备上读取相应数据块. 对于b[i]无效的块号则不用去理它.
//本函数其实可以根据指定的b[]的块号随意读取1~4个数据块
	for (i=0 ; i<4 ; i++)
		if (b[i]) {  //块号有效
			if (bh[i] = getblk(dev,b[i]))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
//随后将4个缓冲块上的内容顺序复制到指定地址处, 在进行复制(使用)缓冲块之前先要睡眠等待缓冲块解锁,另外因为可能睡眠过了,所以还需要在复制前
//检查一下缓冲块中的数据是否是有效的,复制完后还需要释放缓冲块
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]); //等待缓冲块解锁
			if (bh[i]->b_uptodate)  // 若缓冲块中数据有效的话则复制
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);  //释放该缓冲区
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other //breada可以像bread一样使用,但会另外预读一些块. 该函数
 * blocks for reading as well. End the argument list with a negative //参数列表需要使用一个负数来表明参数列表的结束
 * number.
 */
//从指定设备读取指定的一些块; 函数参数个数可变,是一系列指定的块号,成功时返回第1块的缓冲块头指针. 否则返回NULL
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;
//首先取可变参数表中第1个参数(块号), 接着从高速缓冲区中取指定设备和块号的缓冲块. 如果该缓冲块数据无效(更新标志未置位),则发出读设备数据块请求
	va_start(args,first);
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
//顺序取可变参数表中其他预读块号,并作与上面同样处理,但不引用. line336有一个bug, 其中的bh应该是tmp. 因为这里是预读随后的数据块,只需
//读进高速缓冲区但并不马上就使用, 所以第line337 需要将其引用计数递减释放掉该块(因为getblk()会增加引用计数值)
	while ((first=va_arg(args,int))>=0) {
		tmp=getblk(dev,first);
		if (tmp) {
			if (!tmp->b_uptodate)
				ll_rw_block(READA,bh);  // line336 bh应该是tmp
			tmp->b_count--;  // 暂时释放掉该预读块
		}
	}
//可变参数表中所有参数处理完毕. 于是等待第1个缓冲取解锁. 等待退出之后如果缓冲区中数据仍然有效, 则返回缓冲区头指针退出. 否则释放该缓冲区
//返回NULL, 退出	
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return (NULL);
}
//缓冲区初始化函数. buffer_end是缓冲区内存末端. 对于具有16M内存的系统,缓冲区末端被设置为4M. 对于有8M内存的系统, 缓冲区末端被设置为2M
//该函数从缓冲区开始位置start_buffer处和缓冲区末端buffer_end处分别同时设置(初始化)缓冲块头结构和对应的数据块,直到缓冲区中所有内存被
//分配完毕
void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;
//根据参数提供的缓冲区高端位置确定实际缓冲区高端位置b. 如果缓冲区高端等于1M, 则因为从640K-1M被显存和BIOS占用, 所以实际可用缓冲区
//内存高端位置应该是640K, 否则缓冲区内存高端一定>1M.
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;
//用于初始化缓冲区,建立空闲缓冲块循环链表,并获取系统中缓冲块数目. 操作的过程是从缓冲区高端开始划分1K大小的缓冲块. 同时在缓冲区低端建立
//描述该缓冲块的结构buffer_head,并将这些buffer_head组成双向链表. h是指向缓冲头结构的指针, h+1是指向内存地址连续的下一个缓冲头指针
//也可以说是指向h缓冲头的末端外. 为了保证有足够长度的内存来存储一个缓冲头结构, 需要b所指向的内存地址 >= h缓冲头的末端, 即要求 >=h+1
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0; //使用该缓冲块的设备号
		h->b_dirt = 0;  // 脏标志,即缓冲块修改标志
		h->b_count = 0;  // 缓冲块引用计数
		h->b_lock = 0; //缓冲块锁定标志
		h->b_uptodate = 0;  // 缓冲块更新标志, 数据有效标志
		h->b_wait = NULL;  // 指向等待该缓冲块解锁的进程
		h->b_next = NULL;  // 指向具有相同hash值的下一个缓冲头
		h->b_prev = NULL;  // 指向具有相同hash值的前一个缓冲头
		h->b_data = (char *) b;  // 指向对应缓冲块数据(1024字节)
		h->b_prev_free = h-1;  // 指向链表中前一项
		h->b_next_free = h+1;  // 指向链表中下一项
		h++;  // h指向下一新缓冲头位置
		NR_BUFFERS++;  // 缓冲区块数累加
		if (b == (void *) 0x100000)  // 若b递减到=1M,则跳过384K
			b = (void *) 0xA0000;  // 让b指向地址0xA0000(640K)处
	}
	h--;  // 让h指向最后一个有效缓冲块头
	free_list = start_buffer; // 让空闲链表头指向头一个缓冲块
	free_list->b_prev_free = h;  // 链表头的b_prev_free 指向前一项(即最后一项)
	h->b_next_free = free_list;  // h的下一项指针指向第一项,形成一个环链
	for (i=0;i<NR_HASH;i++)  // 初始化hash表,置表中所有指针为NULL
		hash_table[i]=NULL;
}	
