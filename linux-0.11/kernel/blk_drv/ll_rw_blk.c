/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];  // 请求项队列数组, 共NR_REQUEST=32个请求项

/*
 * used to wait on when there are no free requests
 */
struct task_struct * wait_for_request = NULL; // 用于在请求数组中没有空闲项时进程的临时等待处

/* blk_dev_struct is:
 *	do_request-address  // 对应主设备号的请求处理程序指针
 *	next-request  // 该设备的下一个请求
 */
// 块设备数组. 该数组使用主设备号作为索引. 实际内容将在各设驱动程序初始化时填入.
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */ // 0-无设备
	{ NULL, NULL },		/* dev mem */  // 1-内存
	{ NULL, NULL },		/* dev fd */  // 2-软驱
	{ NULL, NULL },		/* dev hd */ // 3-硬盘
	{ NULL, NULL },		/* dev ttyx */ //4 ttyx
	{ NULL, NULL },		/* dev tty */  // 5 tty
	{ NULL, NULL }		/* dev lp */ // 6 lp打印机设备
};
//锁定指定缓冲块; 如果指定的缓冲块以被其他任务锁定,则自己睡眠(不可中断地等待), 直到被执行解锁缓冲块的任务明确地唤醒
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();  // 清中断许可
	while (bh->b_lock)
		sleep_on(&bh->b_wait);  // 如果缓冲区已被锁定则睡眠, 直到缓冲区解锁
	bh->b_lock=1; // 立刻锁定该缓冲区
	sti();  // 开中断
}
//释放(解锁)锁定的缓冲区; 该函数与blk.h文件中的同名函数完全一样
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)  // 如果该缓冲区没有被锁定,则打印出错信息
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;  // 清锁定标志
	wake_up(&bh->b_wait);  // 唤醒等待该缓冲区的任务
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
//向链表中加入请求项. 参数dev是指定块设备结构指针, 该结构中有处理请求项函数指针和当前正在请求项指针. req是已设置好内容的请求项结构指针
//本函数把已经设置好的请求项req添加到指定设备的请求项链表中. 如果该设备的当前请求项指针为空,则可以设置req为当前请求项并立刻调用设备请求
//项处理函数, 否则就把req请求项插入到该请求项链表中
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;
//首先再进一步对参数提供的请求项的指针和标志作初始设置,置空请求项中的下一请求项指针,关中断并清除请求项相关缓冲区脏标志.
	req->next = NULL;
	cli(); // 关中断
	if (req->bh)
		req->bh->b_dirt = 0;  // 清缓冲区"脏"标志
//查看指定设备是否有当前请求项, 即查看设备是否正忙.如果指定设备dev当前请求项子段为空,则表示目前该设备没有请求项,本次是第1个请求项,也是
//唯一的一个,因此可以将块设备当前请求指针直接指向该请求项,并立刻执行相应设备的请求函数.
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti();//开中断
		(dev->request_fn)(); //执行请求函数,对于硬盘是do_hd_request()
		return;
	}
//如果目前该设备已经有当前请求项在处理,则首先利用电梯算法搜索最佳插入位置,然后将当前请求插入到请求链表中,最后开中断并退出函数. 电梯算法
//的作用是让磁盘磁头的移动距离最小, 从而改善(减少)磁盘访问时间.
	for ( ; tmp->next ; tmp=tmp->next)
		if ((IN_ORDER(tmp,req) ||  // 把req所指请求项与请求队列(链表)中已有的请求项作比较,找出req插入该队列的正确位置顺序,
		//然后中断循环,并把req插入到该队列正确位置处
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;
	sti();
}
//创建请求项并插入请求队列中. major是主设备号, rw 指定命令; bh 是存放数据的缓冲区头指针
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;  // 逻辑值,用于判断是否为READA或WRITEA 命令

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
//READA，WRITEA后的A字符代表Ahread,表示提前预读、写数据块的意思. 该函数首先对命令READA/WRITEA的情况进行一些处理, 对于这两个命令,
//当指定的缓冲区正在使用而已被上锁时,就放弃预读写请求.否则就作为普通的read/write命令进行操作. 如果参数给出的命令不是read或write,
//则表示内核程序有错误,显示错误信息并停机. 注意:在修改命令之前这里已为参数是不是预读、写命令设置了标志rw_ahead
	if (rw_ahead = (rw == READA || rw == WRITEA)) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	lock_buffer(bh);
//对命令rw进行处理后,现在只有READ、WRITE两种命令,在开始生成和添加相应读写数据请求项之前,再来看看此次是否有必要添加请求项.在两种情况下
//可以不必添加请求项. 1当命令是WRITE,但缓冲区中的数据在读入之后并没有被修改过;2命令是READ,但缓冲区中的数据是更新过的,即与块设备上的
//完全一样.因此这里首先锁定缓冲区对其进行检查,如果此时缓冲区已被上锁,则当前任务会睡眠,直到被明确的唤醒. 如果确实属于上述两种情况,那么
//就可以直接解锁缓冲区,并返回.这几行代码体现了高速缓冲区的用意.在数据可靠的情况下,就无需在执行硬盘操作,而直接使用内存中的现有数据.缓冲块
//的b_dirt标志用意表示缓冲区中的数据是否以被修改过.b_uptodate标志表示缓冲块的数据是与块设备上的同步,即在从块设备上读入缓冲块后没有修改过
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads. 不能让队列中全是写请求;读操作是优先的,请求队列的后1/3空间用于读请求项
 */
//现在必须为本函数生成并添加读写请求项了.首先需要在请求数组中寻找到一个空闲项来存放新请求项.搜索过程从请求数组末端开始.根据要求,对于读
//命令请求,直接从队列末尾开始搜索,对于写请求就只能从队列2/3处向队列头处搜索空项填入.于是从后向前搜索,当请求结构request的设备字段dev=-1
//时,表示该项未占用,如果没有一项是空闲的,此时请求项数组指针已经搜索越过头部,则查看此次请求是否是提前读写,如果是则放弃此次请求操作,
//否则让本次请求操作先睡眠(以等待请求队列腾出空项), 过一会儿再来搜索请求队列.
	if (rw == READ)
		req = request+NR_REQUEST;  // 对应读请求,将指针指向队列尾部
	else
		req = request+((NR_REQUEST*2)/3);  // 写请求,指针指向队列2/3处,搜索一个空请求项
/* find an empty request */
	while (--req >= request)
		if (req->dev<0)
			break;
/* if none found, sleep on new requests: check for rw_ahead */ //未有空闲项,则让该次新请求操作睡眠,需检测是否提前读写
	if (req < request) {  // 如果已搜索到头,队列无空项
		if (rw_ahead) {  // 若是提前读写请求,就退出
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);  // 否则睡眠,过会再查看请求队列.
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */ // 向空闲请求项中填写请求信息,并将其加入对列中
//执行到此处表示已找到一个空闲请求项,于是在设置好的新请求项后调用add_request()把它添加到请求队列中,立马退出.
	req->dev = bh->b_dev;  // 设备号
	req->cmd = rw;  //命令
	req->errors=0;  // 操作时参数的错误次数
	req->sector = bh->b_blocknr<<1;  // 起始扇区, 块号转换成扇区号(1块=2扇区)
	req->nr_sectors = 2;  //本请求项需要读写的扇区数
	req->buffer = bh->b_data;  // 请求项缓冲区指针指向需读写的数据缓冲区
	req->waiting = NULL;  // 任务等待操作执行完成的地方
	req->bh = bh;  // 缓冲块头指针
	req->next = NULL;  // 指向下一个请求项
	add_request(major+blk_dev,req);  // 将请求项加入队列中
}
//低层读写数据块函数(low level read write block),该函数是块设备驱动程序与系统其他部分的接口函数.主要功能是创建块设备读写请求项并
//插入到指定块设备请求队列中.实际的读写操作则是由设备的request_fn()完成. 在调用该函数之前,调用者需要首先把读写块设备的信息保存在缓冲块
//头结构中,如设备号、块号, rw: read,write,reada,writea, bh:数据缓冲块头指针
void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major; //主设备号
//如果设备主设备号不存在或该设备的请求操作函数不存在,则显示出错信息,并返回. 否则创建请求项并插入请求对列中.
	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}
// 块设备初始化函数,由初始化程序 main.c调用. 初始化请求数组,将所有请求项置空闲项(dev=-1),共32项.
void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
