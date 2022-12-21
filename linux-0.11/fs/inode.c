/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h> //内存管理头文件
#include <asm/system.h>

struct m_inode inode_table[NR_INODE]={{0,},};  //内存中i节点表(NR_INODE=32项)

static void read_inode(struct m_inode * inode); //读指定i节点号的i节点信息
static void write_inode(struct m_inode * inode); //写i节点信息到高速缓冲中
//等待指定的i节点可用; 如果i节点已被锁定,则将当前任务置为不可中断的等待状态,并添加到该i节点
//的等待队列i_wait中, 直到该i节点解锁并明确地唤醒本任务
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}
//对指定的i节点上锁(锁定指定的i节点); 如果i节点已被锁定,则将当前任务置为不可中断的等待状态
//并添加到该i节点的等待队列i_wait中, 知道该i节点解锁并明确的唤醒本任务, 然后对其上锁
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1; //置锁定标志
	sti();
}
//对指定的i节点解锁; 复位i节点的锁定标志,并明确地唤醒等待在此i节点等待队列i_wait上的所有进程
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}
//释放设备dev在内存i节点表中的所有i节点. 扫描内存中的i节点表数组,如果是指定设备使用的i节点就释放之.
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;
//让指针指向内存i节点表数组首项,然后扫描i节点表指针数组中的所有i节点. 针对其中每个i节点,先
//等待该i节点解锁可用(若目前正被上锁的话), 在判断是否属于指定设备的i节点. 如果是指定设备的i
//节点,则看看它是否还被使用着, 即其引用计数是否不为0. 若是则显示警告信息,然后释放之,即把i
//节点的设备号字段i_dev置0. 0+inode_table 等同于 inode_table, &inode_table[0]
	inode = 0+inode_table;  // 指向i节点表指针数组首项
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);  // 等待该i节点可用(解锁)
		if (inode->i_dev == dev) {
			if (inode->i_count) // 若其引用数!=0, 则显示出错警告
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;  // 释放i节点(置设备号=0)
		}
	}
}
//同步所有i节点; 把内存i节点表中所有i节点与设备上i节点作同步操作
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;
//让内存i节点类型的指针指向i节点表首项,然后扫描整个i节点表中的节点. 针对其中每个i节点,先
//等待该i节点解锁可用,然后在判断该i节点是否已被修改并且不是管道节点. 若是这种情况则将该i节点
//写入高速缓冲区中,缓冲区管理程序buffer.c会在适当时机将它们写入盘中
	inode = 0+inode_table;  // 让指针首先指向i节点表指针数组首项
	for(i=0 ; i<NR_INODE ; i++,inode++) { //扫描i节点表指针数组
		wait_on_inode(inode);  // 等待i节点可用
		if (inode->i_dirt && !inode->i_pipe) //若i节点已修改且不是管道节点
			write_inode(inode); //则写盘(实际是写入缓冲区中)
	}
}
//文件数据块映射到盘块的处理操作; block位图处理函数, bmap: block map
// inode 文件的i节点指针; block 文件中的数据块号; create 创建块标志; 该函数把指定的文件数据块
//block对应到设备上逻辑块上,并返回逻辑块号. 如果创建标志置位, 则在设备上对应逻辑块不存在时
//就申请新磁盘块,返回文件数据块block对应在设备上的逻辑块号(盘块号)
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;
//先判断参数文件数据块号block的有效性, 如果块号<0,则停机. 块号>直接块数+间接块数+二次间接块数
//超出文件系统表示范围,停机
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
//根据文件块号的大小值和是否设置了创建标志分别进行处理, 如果该块号<7, 则使用直接块表示. 如果
//创建标志置位,且i节点中对应块的逻辑块(区段)字段=0,则向相应设备申请一磁盘块(逻辑块),且将盘上
//逻辑块号(盘块号)填入逻辑块字段中. 然后设置i节点改变时间, 置i节点已修改标志. 最后返回逻辑块号
	if (block<7) {
		if (create && !inode->i_zone[block])
			if (inode->i_zone[block]=new_block(inode->i_dev)) {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1; //设置已修改标志
			}
		return inode->i_zone[block];
	}
//块号>=8,且小于7+512, 则说明使用的是一次间接块. 下面对一次间接块进行处理. 如果是创建,且该
//i节点中对应间接块字段i_zone[7]是0, 表明文件是首次使用间接块,则需要申请一磁盘块用于存放间接
//块信息, 并将此实际磁盘块号填入间接块字段中, 然后设置i节点已修改标志和修改时间. 如果创建时申请
//磁盘块失败, 则此时i节点间接块字段i_zone[7]=0, 返回0. 或者不是创建, 但i_zone[7]原来=0,表明
//i节点中没有间接块, 于是映射磁盘块失败, 返回0退出.
	block -= 7;
	if (block<512) {
		if (create && !inode->i_zone[7])
			if (inode->i_zone[7]=new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_zone[7])
			return 0;
//读取设备上该i节点的一次间接块, 取该间接块上第block项中的逻辑块号(盘块号)i, 每项2个字节
//如果时创建且间接块的第block项中的逻辑块号=0, 则申请一磁盘块,并让间接块中的第block项等于
//该新逻辑块号, 然后置位间接块的已修改标志. 如果不是创建, 则i就是需要映射(寻找)的逻辑块号
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
		i = ((unsigned short *) (bh->b_data))[block];
		if (create && !i)
			if (i=new_block(inode->i_dev)) {
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
//释放该间接块占用的缓冲块,并返回磁盘上新申请或原有的对应block的逻辑块号
		brelse(bh);
		return i;
	}
//数据属于二次间接块. 其处理过程与一次间接块类似. 首先将block再减去间接块所容纳的块数(512)	
//然后根据是否设置了创建标志进行创建或寻找处理. 如果是新创建且i节点的二次间接块字段=0,则需要
//申请一块磁盘块用于存放二次间接块的一级块信息, 并将此实际磁盘块号填入二次间接块字段中. 之后
//置i节点已修改编制和修改实际. 如果创建时申请磁盘块失败, 则此时i节点二次间接块字段i_zone[8]=0
//返回0. 或者不是创建, 但i_zone[8]原来就为0, 表明i节点中没有间接块, 于是映射磁盘块失败,返回0退出
	block -= 512;
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8]=new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
//读取设备上该i节点的二次间接块,并取该二次间接块的一级块上第(block/512)项中的逻辑块号i,
//如果是创建且二次间接块的一级块上第block/512项中的逻辑块号=0, 则需要申请一磁盘块(逻辑块)
//作为二次间接块的二级块i, 并让二次间接块的一级块中第block/512项等于该二级块的块号i. 然后
//置位二次间接块的一级块已修改标志. 并释放二次间接块的一级块. 如果不是创建, 则i就是需要映射
//(寻找)的逻辑块号
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	i = ((unsigned short *)bh->b_data)[block>>9];
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
//如果二次间接块的二级块块号=0, 表示申请磁盘块失败或对应块号=0, 则返回0退出. 否则就从设备上
//读取二次间接块的二级块,并取该二级块上第block项中的逻辑块号(与上511是为了限定block值不超过511)	
	if (!i)
		return 0;
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
//如果是创建且二级块的第block项中逻辑块号=0,则申请一磁盘块(逻辑块), 作为最终存放数据信息
//的块, 并让二级块中的第block项等于该新逻辑块块号i, 然后置位二级块的已修改标志
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh); //释放该二次间接块的二级块,返回磁盘上新申请的或原有的对应block的逻辑块号
	return i;
}
//取文件数据块block在设备上对应的逻辑块号. inode 文件的内存i节点指针; block 文件中的数据块号
//若操作成功则返回对应的逻辑块号,否则返回0
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}
//取文件数据块block在设备上对应的逻辑块号; 如果对应的逻辑块不存在就创建一块,返回设备上对应
//的已存在或新建的逻辑块号; inode 文件的内存i节点指针; block 文件中的数据块号
//操作成功则返回对应的逻辑块号,否则返回0
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}
//放回(放置)一个i节点(回写入设备);用于把i节点引用计数值递减1,且若是管道i节点,则唤醒等待的进程
//若是块设备文件i节点则刷新设备. 且若i节点的链接计数=0, 则释放该i节点占用的所有磁盘逻辑块,并释放该i节点
void iput(struct m_inode * inode)
{
//先判断参数给出的i节点有效性,并等待inode节点解锁, 如果i节点的引用计数=0,表示该i节点已经是空闲的
//内核再要求对其进行放回操作,说明内核中其他代码有问题, 于是显示错误信息并停机
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count)
		panic("iput: trying to free free inode");
//如果是管道i节点,则唤醒等待该管道的进程,引用次数-1, 如果还有引用则返回. 否则释放管道占用
//的内存页面, 并复位该节点的引用计数值、已修改标志和管道标志,并返回. 对于管道节点, 
//inode->i_size 存放着内存页地址.
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		if (--inode->i_count)
			return;
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
//如果i节点对应的设备号=0,则将此节点的引用计数-1, 返回; 如用于管道操作的i节点,其i节点的设备号=0
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
//如果是块设备文件的i节点,此时逻辑块字段0(i_zone[0])中是设备号, 则刷新该设备,并等待i节点解锁
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
//如果i节点的引用计数>1,则计数-1后就直接返回(因为该i节点还有人在用,不能释放), 否则就说明
//i节点的引用计数值=1(因为前面已经判断过引用计数是否=0), 如果i节点的链接数=0,则说明i节点
//对应文件被删除,于是释放该i节点的所有逻辑块,并释放该i节点. free_inode()用于实际释放i节点
//操作, 即复位i节点对应的i节点位图比特位, 清空i节点结构内容
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	if (!inode->i_nlinks) {
		truncate(inode);
		free_inode(inode);
		return;
	}
//如果该i节点已做过修改,则回写更新该i节点,并等待该i节点解锁. 由于这里在写i节点时需要等待睡眠
//此时其他进程有可能修改该i节点, 因此在进程被唤醒后需要再此重复进行上述判断过程(repeat)
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode); //因为睡眠了, 所以需要重复判断
		goto repeat;
	}
//程序若能指向到此, 则说明该i节点的引用计数值i_count=1, 链接数!=0,且内容没有被修改过,因此
//此时只要把i节点引用计数递减1,返回,此时i节点的i_count=0, 表示已释放
	inode->i_count--;
	return;
}
//从i节点表(inode_table)中获取一个空闲i节点项; 寻找引用计数count为0的i节点, 并将其写盘后
//清零, 返回其指针,引用计数置1
struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table; //指向i节点表第1项
	int i;
//在初始化last_inode指针指向i节点表头一项后循环扫描整个i节点表,如果last_inode已经指向i节点
//表的最后一项后, 则让其重新指向i节点表开始处, 以继续循环寻找空闲i节点项. 如果last_inode所
//指向的i节点的计数值=0, 则说明可能找到空闲i节点项. 让inode指向该i节点. 如果该i节点的已修改
	do {//标志和锁定标志均为0, 则可以使用该i节点,于是退出for循环
		inode = NULL;
		for (i = NR_INODE; i ; i--) {  //NR_INODE=32
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
//没有找到空闲i节点(inode=NULL),则将i节点表打印出来供调试使用,并停机
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
//等待该i节点解锁,如果该i节点已修改标志被置位,则将该i节点刷新(同步), 因为刷新时可能会睡眠
//因此需要再次循环等待该i节点解锁
		wait_on_inode(inode);
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
//如果i节点又被其他占用的话(i节点的计数值不为0了), 则重新寻找空闲i节点,否则说明已找到复合
//要求的空闲i节点项, 则将该i节点项内容清0,并置引用计数为1,返回该i节点指针
	} while (inode->i_count);
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}
//获取管道节点,先扫描i节点表,寻找一个空闲i节点项. 然后取得一页空闲内存供管道使用, 然后将得
//到的i节点的引用计数数置为2(读和写), 初始化管道头和尾, 置i节点的管道类型表示, 返回为i节点
//指针, 如果失败则返回NULL
struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;
//从内存i节点表中取得一个空闲ijied. 如果找不到空闲i节点则返回NULL, 然后为该i节点申请一页内存
//并让节点的i_size 字段指向该页面, 如果已经没有空闲内存,则释放该i节点,并返回NULL
	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(inode->i_size=get_free_page())) {  // 节点的i_size字段指向缓冲区
		inode->i_count = 0;
		return NULL;
	}
//然后设置该i节点的引用计数=2, 并复位管道头尾指针. i节点逻辑块号数组i_zone[]的i_zone[0]
//和i_zone[1]中分别用来存放管道头和管道尾指针, 最后设置i节点时管道i节点标志并返回该i节点
	inode->i_count = 2;	/* sum of readers/writers */  //读写两者总计
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;  // 复位管道头尾指针
	inode->i_pipe = 1; // 置节点为管道使用的标志
	return inode;
}
//取得一个i节点, dev设备号, nr i节点号; 从设备上读取指定节点号的i节点到内存i节点表中,并
//返回该i节点指针; 首先在位于高速缓冲区中的i节点表中搜索, 若找到指定节点号的i节点在经过一些
//判断处理后返回该i节点指针. 否则从设备dev上读取指定i节点号的i节点信息放入i节点表中,并返回该i节点指针
struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;
//判断参数有效性,若设备号=0, 则表明内核代码问题, 显示出错信息并停机. 然后预先从i节点表中取一个
//空闲i节点备用
	if (!dev)
		panic("iget with dev==0");
	empty = get_empty_inode();
//接着扫描i节点表, 寻找参数指定节点号nr的i节点,并递增该节点的引用次数. 如果当前扫描i节点的设备
//号!=指定的设备号或节点号 != 指定的节点号, 则继续扫描	
	inode = inode_table;
	while (inode < NR_INODE+inode_table) {
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
//找到指定设备号dev和节点号nr的i节点,则等待该节点解锁, 在等待该节点解锁过程中,i节点表可能
//会发生变化, 所以再次进行上述相同判断, 如果发生了变化, 则再次重新扫描整个i节点表
		wait_on_inode(inode);
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
//表示找到了相应的i节点,于是将该i节点引用计数+1, 然后再作进一步检查, 看它是否是另外一个文件系统
//的安装点.若是则寻找被安装文件系统根节点并返回,如果该i节点的确是其他文件系统的安装点,则再超级块
//表中寻找安装在此i节点的超级块,如果没有找到,则显示出错信息,并放回本函数开始时获取的空闲节点empty
//返回该i节点指针
		inode->i_count++;
		if (inode->i_mount) {
			int i;

			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
//已经找到安装到inode节点的文件系统超级块, 于是将该i节点写盘放回, 并从安装在此i节点上的文件系统
//超级块中取设备号,令i节点号为ROOT_INO, 即=1. 然后重新扫描整个i节点表,以获取该被安装文件系统的
//根i节点信息
			iput(inode);
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}
//找到了相应的i节点,因此可以放弃本函数开始处临时申请的空闲i节点,返回找到的i节点指针			
		if (empty)
			iput(empty);
		return inode;
	}
//在i节点表中没有找到指定的i节点,则利用前面申请的空闲i节点empty在i节点表中建立该i节点,
//并从相应设备上读取该i节点信息,返回该i节点指针
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev; //设置i节点的设备
	inode->i_num = nr;  // 设置i节点号
	read_inode(inode);
	return inode;
}
//读取指定i节点信息;从设备上读取含有指定i节点信息的i节点盘块,然后复制到指定的i节点结构中
//为了确定i节点所在的设备逻辑块号(或缓冲块),必须首先读取相应设备上的超级块, 以获取用于计算
//逻辑块号的每块i节点数信息INODES_PER_BLOCK, 在计算出i节点所在的逻辑块号后,把该逻辑块读入
//一缓冲块中, 然后把缓冲块中相应位置处的i节点内容复制到参数指定的位置处
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;
//先锁定该i节点,并取该节点所在设备的超级块.
	lock_inode(inode);
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
//该i节点所在的设备逻辑块 = (启动块+超级块)	+ i节点位图占用的块 + 逻辑块位图占用的块 +
//(i节点号-1)/每块含有的i节点数. 虽然i节点从0开始编号, 但第1个0号i节点不用,且磁盘上也
//不保存对应的0号i节点结构, 因此存放i节点的盘块的第1块上保存的是i节点号是1~16的i节点结构
//而不是0~15的, 因此在计算i节点号对应的i节点结构所在盘块时需要-1, 即:B=(i节点号-1)/每块含有i节点结构数
//如: 节点号16的i节点结构应该在B=(16-1)/16=0的块上, 这里从设备上读取该i节点所在的逻辑块
//并复制指定i节点内容到inode指针所指位置处
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
	brelse(bh);//释放读入的缓冲块,并解锁该i节点
	unlock_inode(inode);
}
//将i节点信息写入缓冲区中. 该函数把参数指定的i节点写入缓冲区相应的缓冲块中,待缓冲区刷新时
//会写入盘中. 为了确定i节点所在的设备逻辑块号(缓冲块),必须首先读取相应设备上的超级块,以获取
//用于计算逻辑块号的每块i节点数信息INODES_PER_BLOCK, 在计算出i节点所在的逻辑块号后,就把该
//逻辑块读入一缓冲块中,然后把i节点内容复制到缓冲块的相应位置处
static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;
//锁定该i节点,如果该i节点没有被修改过或该i节点的设备号=0, 则解锁该i节点,并退出.对于没有被
//修改过的i节点,其内容与缓冲区中或设备中的相同,然后获取该i节点的超级块
	lock_inode(inode);
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");
//该i节点所在的设备逻辑块号 = (启动块+超级块)+ i节点位图占用的块 + 逻辑块位图占用的块 +
//(i节点号-1)/每块含有的i节点数. 从设备上duqi节点所在的逻辑块,并将该i节点信息复制到逻辑块
//对应该i节点的项位置处
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	bh->b_dirt=1;//置缓冲区已修改标志, 而i节点内容已经与缓冲区中的一致,因此修改标志置0
	inode->i_dirt=0; //然后释放该含有i节点的缓冲区,并解锁该i节点
	brelse(bh);
	unlock_inode(inode);
}
