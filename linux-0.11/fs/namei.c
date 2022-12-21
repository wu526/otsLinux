/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h> //文件控制头文件,文件及其描述符的操作控制常数符号的定义
#include <errno.h>
#include <const.h> //常数符号头文件. 目前仅定义i节点中i_mode字段的各标志位
#include <sys/stat.h> //文件状态头文件. 含有文件或文件系统状态结构stat{}和常量
//该宏的右侧表达式是访问数组的一种特殊使用方法, 基于这样的一个事实. 即用数组名和数组下标所
//表示的数组项(如a[b])的值等同于使用数组首指针(地址)加上该偏移地址的形式*(a+b).同时a[b]也
//可以表示为b[a]的形式, 因此对于字符数组项形式为"LoveYou"[2](或2["LoveYou"])就等同于
//*("LoveYou"+2). 字符串"LoveYou"在内存中被存储的位置就是其地址,因此数组项"LoveYou"[2]的
//值就是该字符串中索引值=2的字符"v"所对应的ascii码. C中字符也可以使用其ascii码值来表示,
//方法是在字符的ascii码值前面加一个反斜杠,如字符"v"可以表示为\x76或者\166. 因此对于不可显示
//字符, 就可以用其ascii码值来表示. 访问模式宏, x 是include/fcntl.h中定义的文件访问(打开)标志
//这个宏根据文件访问标志x的值来索引双引号中对应的数值. 双引号中有4个8进制数值, 分别表示读、写
//和执行的权限为 r,w,rw,和rwxrwxrwx,并且分别对应x的索引值0~3. 如: 如果x=2, 则该宏返回8进制
//值006,表示可读写, O_ACCMODE=0003,是索引值x的屏蔽码
#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed. 如果想让文件名长度>NAME_LEN个的字符被截掉
 */ //就将下面定义注释去掉
/* #define NO_TRUNCATE */

#define MAY_EXEC 1 //可执行(可进入)
#define MAY_WRITE 2 //可写
#define MAY_READ 4 //可读

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
//检测文件访问许可权限. inode文件的i节点指针; mask: 访问属性屏蔽码; 返回:访问许可返回1,否则返回0
static int permission(struct m_inode * inode,int mask)
{
	int mode = inode->i_mode; //文件访问属性
//即使是超级用户也不能读写一个已被删除的文件
/* special case: not even root can read/write a deleted file */
//如果i节点有对应的设备, 但该i节点的链接计数值=0,表示该文件已被删除,则返回
//否则如果进程的有效用户id(euid)与i节点的用户id相同,则取文件宿主的访问权限.
//否则如果进程的有效组id(egid)与i节点的组id相同,则取组用户的访问权限
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	else if (current->euid==inode->i_uid)
		mode >>= 6;
	else if (current->egid==inode->i_gid)
		mode >>= 3;
//判断如果所取的访问权限与屏蔽码相同,或者是超级用户,则返回1,否则返回0		
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 * 不能使用strncmp字符串比较函数,因为名称不在我们的数据空间(不在内核空间)因而我们只能
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */ //使用match(), 与strncmp不同的是, match()成功返回1, 失败时返回0
//指定长度字符串比较函数; len比较的字符串长度; name文件名指针; de 目录项结构
//返回:相同返回1, 不同返回0
static int match(int len,const char * name,struct dir_entry * de)
{
	register int same __asm__("ax");
//判断函数参数的有效性. 如果目录项指针为NULL,或者目录项i节点=0,或者要比较的字符串长度超过
//文件名长度,则返回0. 如果要比较的长度len<NAME_LEN,但目录项中文件名长度超过len,也返回0
//line69对目录项中文件名长度是否超过len的判断方法是检测name[len]是否为NULL. 若长度超过
//len,则name[len]处就是一个不是NULL的普通字符,对于长度为len的字符串name, 字符name[len]应该是NULL
	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	if (len < NAME_LEN && de->name[len]) //line69
		return 0;
//会在用户数据空间fs段执行字符串的比较操作. %0 eax 比较结果(same); %1 eax 初始值0;
//%2 esi 名字指针; %3 edi 目录项名指针; %4 ecx 比较的字节长度值len
	__asm__("cld\n\t"
		"fs ; repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		:"cx","di","si");
	return same; //返回比较结果
}

/*
 *	find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
//查找指定目录和文件名的目录项; *dir指定目录i节点的指针; name 文件名; namelen文件名长度
//该函数在指定目录的数据(文件)中搜索指定文件名的目录项,并对指定文件名是".."的情况根据当前进行
//的相关设置进行特殊处理. 返回:成功则函数高速缓冲区指针,并在*res_dir处返回的目录项结构指针
//失败则返回空指针NULL
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;
//对函数参数的有效性进行判断和验证. 如果在前面定义了NO_TRUNCATE, 那么如果文件名长度超过最大长度
//NAME_LEN, 则不予处理,如果没有定义该宏,那么在文件名长度超过最大长度NAME_LEN时截断.
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
//计算本目录中目录项项数entries, 目录i节点i_size中包含有本目录包含的数据长度. 因此其除以一个
//目录项的长度(16字节)即可得到该目录中目录项数. 然后置空返回目录项结构指针. 如果文件名长度
//等于0,则返回NULL退出
	entries = (*dir)->i_size / (sizeof (struct dir_entry));
	*res_dir = NULL;
	if (!namelen)
		return NULL;
//对目录项文件名是".."的情况进行特殊处理. 如果当前进程指定的根i节点就是函数参数指定的目录,则
//说明对于本进程来说,这个目录就是它的伪根目录,即进程只能访问该目录中的项而不能后退到其父目录中
//即对于该进程本目录就如同是文件系统的根目录. 因此需要将文件名修改为"." 否则如果该目录的i节点
//号=ROOT_INO(1号)的话,说明确实是文件系统的根i节点. 则取文件系统的超级块, 如果被安装到的i节点
//存在, 则先放回原i节点, 然后对被安装到的i节点进行处理. 于是让*dir指向该被安装到的i节点,且
//该i节点的引用数加1. 即针对这种情况,悄悄的进行了"偷梁换柱"工程.
/* check for '..', as we might have to do some "magic" for it */ //检测目录项"..", 因为可能需要对其进行特殊处理
	if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */ // 伪根中的".."如同一个假".", 只需改变名字长度
		if ((*dir) == current->root)
			namelen=1;
		else if ((*dir)->i_num == ROOT_INO) {
/* '..' over a mount-point results in 'dir' being exchanged for the mounted 在一个安装点上的".."将导致目录交换到被安装文件系统的目录
i节点上, 注意: 由于设置了mounted标志, 因而能够放回该新目录
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
			sb=get_super((*dir)->i_dev);
			if (sb->s_imount) {
				iput(*dir);
				(*dir)=sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}
//开始正常操作,查找指定文件名的目录项在什么地方.因此需要读取目录的数据,即取出目录i节点对应块	
//设备数据区中的数据块(逻辑块)信息. 这些逻辑块的块号保存在i节点结构的i_zone[9]数组中,先取其中
//第1个块号,如果目录i节点指向的第1个直接磁盘块号=0, 则说明该目录竟然不含数据,这不正常.于是返回
//NULL退出. 否则就从节点所在设备读取指定的目录项数据块,如果不成功,也返回NULL退出
	if (!(block = (*dir)->i_zone[0]))
		return NULL;
	if (!(bh = bread((*dir)->i_dev,block)))
		return NULL;
//此时就在这个读取的目录i节点数据块中搜索匹配指定文件名的目录项. 让de指向缓冲块中的数据块部分
//并在不超过目录中目录项数的条件下,循环执行搜索. 其中i是目录中的目录项索引号,在循环开始时初始化为0
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (i < entries) { //如果当前目录项数据块已经搜索完,还没有找到要匹配的目录项,则释放当前
//目录项数据块. 再读入目录的下一个逻辑块. 若这块为空,则只要还没有搜索完目录中的所有目录项,
//就跳过该块,继续读目录的下一逻辑块. 若该块不空,就让de指向该数据块,然后再其中继续搜索.
//其中line137 上i/DIR_ENTRIES_PER_BLOCK可得到当前搜索的目录项所在目录文件中的块号,bmap()
//则可以计算出在设备上对应的逻辑块号
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) || //line137
			    !(bh = bread((*dir)->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
//如果找到匹配的目录项, 则返回该目录项结构指针de和该目录项i节点指针*dir以及该目录项数据块
//指针bh, 并退出函数. 否则继续在目录项数据块中比较下一个目录项		
		if (match(namelen,name,de)) {
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh); //如果指定目录的所有目录项都搜索完后,还没有找到相应的目录项,则释放目录的
	return NULL; //数据块,最后返回NULL(失败)
}

/*
 *	add_entry()
 *使用与find_entry()同样的方法, 往指定目录中添加一指定文件民的目录项,如果失败则返回NULL
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *de(指定目录项结构指针)的i节点部分被设置为0, 这表示在调用该函数和往目录项中添加信息之间不能
 * NOTE!! The inode part of 'de' is left at 0 - which means you //睡眠,如果睡眠其他进程
 * may not sleep between calling this and putting something into 可能会使用了该目录项
 * the entry, as someone else might have used it while you slept.
 */
//根据指定的目录和文件名添加目录项; dir 指定目录的i节点; name 文件名; namelen 文件名长度;
//返回: 高速缓冲区指针; res_dir 返回的目录项结构指针
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
//对函数参数的有效性进行判断和验证.
	*res_dir = NULL; //用于返回目录项结构指针
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
//开始操作, 向指定目录中添加一个指定文件名的目录项,因此需要先读取目录的数据,即取出目录i节点
//对应块设备数据区中的数据块(逻辑块)信息. 这些逻辑块的块号保存在i节点结构的i_zone[9]数组中
//先取其中第1个块号,如果目录i节点指向的第一个直接磁盘块号=0,则说明该目录不含数据,这不正常,于是返回
//NULL退出; 否则就从节点所在设备读取指定的目录项数据块. 不成功则返回NULL退出. 如果参数提供
//的文件名长度=0,也返回NULL退出
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))
		return NULL;
	if (!(bh = bread(dir->i_dev,block)))
		return NULL;
//此时就在这个目录i节点数据块中循环查询最后未使用的空目录项.先让目录项结构指针de指向缓冲块
//中的数据块部分, 即第一个目录项处,其中i是目录中的目录项索引号,在循环开始时初始化为0.
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) { // 当前目录项数据块已经搜索完毕,但还没有找到需要的空目录项,则释放当前目录项
//数据块,再读入目录的下一个逻辑块. 如果对应的逻辑块不存在就创建一块.若读取或创建操作失败则返回
//空. 如果此次读取的磁盘逻辑块数据返回的缓冲块指针为空,说明这块逻辑块可能时因为不存在而新创建
//的空块,则把目录项索引值加上1块逻辑块所能容纳的目录项数DIR_ENTRIES_PER_BLOCK用以跳过该块并
//继续搜索. 否则说明新读入的块上有目录项数据,于是让目录项结构指针de指向该块的缓冲块数据部分
//然后再其中继续搜索. 其中line192的 i/DIR_ENTRIES_PER_BLOCK 可计算得到当前搜索的目录项i
//所在目录文件中的块号, create_block()则可以读取或创建出在设备上对应的逻辑块
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK); //line192
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev,block))) { //若空则跳过该块继续
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
//如果当前所操作的目录项序号i乘上目录结构大小所得的长度值超过了该目录i节点信息所指出的目录数据
//长度值i_size, 则说明整个目录文件塑胶中没有由于删除文件留下的空目录项,因此只能把需要添加的
//新目录项附加到目录文件数据的末端处,于是对该处目录项进行设置(置该目录项的i节点指针为NULL),
//并更新该目录文件的长度值(加上一个目录项的长度), 然后设置目录的i节点已修改标志,再更新该目录的
//改变时间为当前时间
		if (i*sizeof(struct dir_entry) >= dir->i_size) {
			de->inode=0;
			dir->i_size = (i+1)*sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
//若当前搜索的目录项de的i节点为空,则表示找到一个还未使用的空闲目录项或是添加的新目录项
//于是更新目录的修改时间为当前时间,并从用户数据区复制文件名到该目录项的文件名字段, 置含有
//本目录项的相应高速缓冲块已修改标志. 返回该目录项的指针以及该高速缓冲块的指针,退出
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++; //如果该目录项已经被使用,则继续检测下一个目录项
		i++;
	}
	brelse(bh); //本函数执行不到这里
	return NULL;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure. 函数根据给出的路径名进行搜索,直到到达最顶端的目录
 */ //如果失败则返回NULL
// 搜索直到路径名的目录(或文件名)的i节点; pathname 路径名; 返回目录或文件的i节点指针.失败返回NULL
static struct m_inode * get_dir(const char * pathname)
{
	char c;
	const char * thisname;
	struct m_inode * inode;
	struct buffer_head * bh;
	int namelen,inr,idev;
	struct dir_entry * de;
//从当前进程任务结构中设置的根(或伪根)i节点或当前工作目录i节点开始. 首先需要判断进程的根i节点
//指针和当前工作目录i节点指针是否有效. 如果当前进程没有设定根i节点, 或者该进程根i节点指向的是
//一个空闲i节点(引用=0), 则系统出错停机. 如果进程的当前工作目录i节点指针=NULL, 或者该当前工作
//目录指向的i节点是一个空闲i节点,这也是系统有问题, 停机
	if (!current->root || !current->root->i_count)
		panic("No root inode");
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");
//如果用户指定的路径名第1个字符是'/', 则说明路径名是绝对路径名, 则从根i节点开始操作,否则若第一个
//字符是其他字符,则表示给定的是相对路径名, 应从进程的当前工作目录开始操作. 则取进程当前工作
//目录的i节点,如果路径名=空, 则出错返回NULL. 此时变量indoe指向了正确的i节点-进程的根i节点或
//当前工作目录i节点之一
	if ((c=get_fs_byte(pathname))=='/') {
		inode = current->root;
		pathname++;
	} else if (c)
		inode = current->pwd;
	else
		return NULL;	/* empty name is bad */
//然后针对路径名中的各个目录名部分和文件名进行循环处理. 首先把得到的i节点引用计数+1,表示正在使用
//在循环处理过程中,要先对当前正在处理的目录名部分(或文件名)的i节点进行有效性判断,且把变量
//thisname指向当前正在处理的目录名部分(或文件名), 如果该i阶段不是目录类型的i节点,或者没有可
//进入该目录的访问许可,则放回该i节点并返回NULL. 刚进入循环时,当前的i节点就是进程根i节点或
//当前工作目录的i节点.
	inode->i_count++;
	while (1) {
		thisname = pathname;
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
			iput(inode);
			return NULL;
		}
//每次循环处理路径名中一个目录名(文件名)部分. 因此在每次循环中都要从路径名字符串中分离出一个
//目录名(文件名), 方法是从当前路径名指针pathname开始出检索字符,直到字符是以一个NULL或/字符
//此时变量namelen正好是当前处理目录名部分的长度, 变量thisname正指向该目录名部分的开始处.
//此时如果字符是结尾符nULL,表明已经搜索到路径名末尾,并已到达最后指定目录名或文件名,则返回该
//i结点指针; 注意:如果路径名中最后一个名称也是一个目录名, 但其后面没有加上/字符,则函数不会
//返回该最后目录的i节点. 如: 对于/usr/src/linux 该函数返回 src/目录名的i节点.
		for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
			/* nothing */ ;
		if (!c)
			return inode;
//在得到当前目录名部分(文件名)后,调用查找目录项函数find_entry()在当前处理的目录中寻找指定
//名称的目录项, 如果没有找到,则放回该i节点,并返回NULL. 然后再找到的目录项中取出其i节点号inr
//和inode,并以该目录为当前目录继续循环处理路径名中的下一目录名部分(或文件名)
		if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
			iput(inode);
			return NULL;
		}
		inr = de->inode; //当前目录名部分的i节点号
		idev = inode->i_dev;
		brelse(bh);
		iput(inode);
		if (!(inode = iget(idev,inr))) //取i节点内容
			return NULL;
	}
}

/*
 *	dir_namei()
 *返回指定目录名的i节点指针,以及在最顶层目录的名称.
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
//pathname 目录路径名; namelen 路径名长度; name 返回的最顶层目录名; 返回: 指定目录名最顶层
//目录的i节点指针和最顶层目录名称及长度. 出错返回NULL. 最顶层目录指路径名中最靠近末端的目录
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name)
{
	char c;
	const char * basename;
	struct m_inode * dir;
//取得指定路径名最顶层目录的i节点, 然后对路径名pathname 进行搜索检测, 查出最后一个'/'字符
//后面的名字字符串,计算其长度,并且返回最顶层目录的i节点指针. 如果路径名最后一个字符是/,
//那么返回的目录名为空,且长度=0. 但返回的i节点指针仍然指向最后一个/字符前目录名的i节点.
	if (!(dir = get_dir(pathname)))
		return NULL;
	basename = pathname;
	while (c=get_fs_byte(pathname++))
		if (c=='/')
			basename=pathname;
	*namelen = pathname-basename-1;
	*name = basename;
	return dir;
}

/*
 *	namei()
 *该函数被许多命令用于取得指定路径名称的i节点. open,link等则使用它们自己的相应函数.
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
//取指定路径名的i节点. pathname 路径名. 返回对应的i节点
struct m_inode * namei(const char * pathname)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir;
	struct buffer_head * bh;
	struct dir_entry * de;
//首先查找指定路径的最顶层目录的目录名并得到其i节点,若不存在,则返回NULL.如果返回的最顶层
//名字的长度是0, 则表示该路径名以一个目录名为最后一项. 因此已经找到对应目录的i节点,可以直接
//返回该i节点退出.
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return NULL;
	if (!namelen)			/* special case: '/usr/' etc */
		return dir;
//然后再返回的顶层目录中寻找指定文件名目录项的i节点. 因为如果最后也是一个目录名,但其后没有/
//则不会返回该最后目录的i节点. 因为函数dir_name()把不以/结束的最后一个名字当作一个文件名
//来看待. 所以这里需要单独对这种情况使用寻找目录项i节点函数find_entry()进行处理.
//此时de中含有寻找到目录项指针, dir是包含该目录项的目录的i节点指针
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return NULL;
	}
//取该目录项的i节点号和设备号, 并释放包含该目录项的高速缓冲块并放回目录i节点. 然后取对应节点
//号的i节点, 修改其被访问时间为当前时间, 并置已修改标志,最后返回该i节点指针.
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	dir=iget(dev,inr);
	if (dir) {
		dir->i_atime=CURRENT_TIME;
		dir->i_dirt=1;
	}
	return dir;
}

/*
 *	open_namei()
 * 几乎是完整的打开文件程序
 * namei for open - this is in fact almost the whole open-routine.
 */
//文件打开namei函数; pathname 是文件名, flag是打开文件标志, 如果本调用是创建一个新文件,
//则mode就用于指定文件的许可属性. 对于新创建的文件, 这些属性只应用于将来对文件的访问, 创建了
//只读文件的打开调用也将返回一个可读写的文件句柄. 返回: 成功返回0,否则返回出错码.
//res_inode返回对应文件路径名的i节点指针
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;
//对参数进行合理的处理. 如果文件访问标志是只读,但文件截0标志O_TRUNC却置位了,则在文件打开标志
//中添加只写标志O_WRONLY. 这样作的原因是由于截0标志必须在文件可写情况下才有效. 然后使用当前
//进程的文件访问许可屏蔽码,屏蔽掉给定模式中的相应位,并添加上普通文件标志I_REGULAR.
//该标志将用于打开的文件不存在而需要创建文件时,作为新文件的默认属性.
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	mode &= 0777 & ~current->umask;
	mode |= I_REGULAR;
//根据指定的路径名寻找到对应的i节点,以及最顶端目录名及其长度. 此时如果最顶端目录名长度=0(如/usr/这种路径情况)	
//那么若操作不是读写、创建和文件长度截0, 则表示打开一个目录名文件操作, 于是直接返回该目录的
//i节点并返回0退出. 否则说明进程操作非法,于是放回该i节点,返回出错码
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {			/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}
//根据上面得到的最顶层目录名的i节点dir, 在其中查找取得路径名字字符串中最后的文件名对应的
//目录结构de. 并同时得到该目录项所在的高速缓冲区指针. 如果该高速缓冲指针=NULL, 则表示没有
//找到对应文件名的目录项,因此只可能是创建文件操作. 此时如果不是创建文件则放回该目录的i节点,
//返回出错码退出. 如果用户在该目录没有写的权力,则放回该目录的i节点,返回出错号退出
	bh = find_entry(&dir,basename,namelen,&de); //line360
	if (!bh) {
		if (!(flag & O_CREAT)) {
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}
//现在确定了是创建操作且写操作许可. 因此就在目录i节点对应设备上申请一个新的i节点给路径名上指定的文件使用. 若失败则放回目录的i节点并返回
//没有空间出错码. 否则使用该新i节点,对其进行初始设置; 设置节点的用户id; 对应节点访问模式; 置已修改标志; 然后并在指定目录dir中添加一个新目录项
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir,basename,namelen,&de);
//如果返回的应该含有新目录项的高速缓冲区指针=NULL,则表示添加目录项操作失败.于是将该新i节点的引用连接计数-1, 放回该i节点与目录的i节点并
//返回出错码退出. 否则说明添加目录项操作成功. 于是设置该新目录项的一些初始值: 值i节点号为新申请到的i节点的号码; 并设置高速缓冲区以修改标志
//然后释放该高速缓冲区,放回目录的i节点. 返回新目录项的i节点指针,并成功退出.
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
	}
//若上面line360在目录中取文件名对应目录项结构的操作成功(即bh!=NULL), 则说明指定打开的文件已经存在.于是取出该目录项的i节点号和其所在设备
//号,并释放该高速缓冲区以及放回目录的i节点. 如果此时独占操作标志O_EXCL置位,但现在文件已经存在,则返回文件已存在出错码退出
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)
		return -EEXIST;
//然后读取该目录项的i节点内容,若该i节点是一个目录的i节点且访问模式是只写或读写, 或者没有访问的许可权限, 则放回该i节点,返回访问权限出错码退出		
	if (!(inode=iget(dev,inr)))
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
//接着更新该i节点的访问时间字段为当前时间. 如果设立了截0标志, 则将该i节点的文件长度截为0, 最后返回该目录项i节点的指针,并返回0(成功)	
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}
//窗一个设备特殊文件或普通文件节点(node); 创建名为filename,由mode和dev指定的文件系统节点(普通文件、设备特殊文件或命名管道)
//filename 路径名; mode 指定使用许可及所创建节点的类型; dev 设备号; 成功返回0, 否则返回出错码
int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
//首先检测操作许可和参数有效性并取路径中顶层目录的i节点. 如果不是超级用户,则返回访问许可出错码; 如果找不到对应路径中顶层目录的i节点,
//则返回出错码; 如果最顶端的文件名长度=0, 则说明给出的路径名最后没有指定文件名,放回该目录i节点,返回出错码. 如果在该目录中没有写的权限,
//则放回该目录的i节点,返回访问许可出错码. 如果不是超级用户,则返回访问许可出错码	
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(filename,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
//搜索一下路径名指定的文件是否已经存在, 若存在则不能创建同名文件节点.如果对应路径名上最后的文件名的目录项存在,则释放包该目录项的缓冲区块
//并放回目录的i节点,返回文件已存在的出错码
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
//否则就申请一个新的i节点, 并设置该i节点的属性模式. 如果要创建的是块设备文件或字符设备文件, 则令i节点的直接逻辑块指针0等于设备号. 即
//对于设备文件来说, 其i节点的i_zone[0]中存放的是该设备文件所定义设备的设备号,然后设置该i节点的修改时间、访问时间为当前时间,并设置i节点已修改标志
	inode = new_inode(dir->i_dev);
	if (!inode) { // 不成功则放回目录i节点,返回无空间出错码
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
//为这个新的i节点在目录中添加一个目录项, 如果失败(包含该目录的高速缓冲块指针为NULL), 则放回目录的i节点;把所申请的i节点引用连接计数复位
//并放回该i节点,返回出错码
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
//添加目录项操作成功,于是来设置这个目录项内容, 令该目录项的i节点字段=新i节点号, 并设置高速缓冲区已修改标志, 放回目录和新的i节点, 释放
//高速缓冲区,最后返回0(成功)
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}
//创建一个目录; pathname 路径名; mode 目录使用的权限属性; 成功返回0, 否则返回出错码
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;
//检测操作许可和参数的有效性并取路径名中顶层目录的i节点. 如果不是超级用户,则返回访问许可出错码; 如果找不到对应路径中顶层目录的i节点,
//返回出错码; 如果最顶端的文件名长度=0, 则说明给出的路径名最后没有指定文件名, 放回该目录i节点,返回出错码; 如果在该目录中没有写权限,
//则放回该目录的i节点,返回许可访问出错码;
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
//搜索路径名指定的目录名是否已存在. 若已经存在则不能创建同名目录节点. 如果对应路径名上最后的目录名项目已经存在,则释放该目录项的缓冲区块
//并放回目录的i节点,返回文件已存储的出错码; 否则就申请一个新的i节点,并设置该i节点的属性模式; 置该新i节点对应的文件长度为32字节(2个目录
//项的大小)、置节点已修改标志, 以及节点的修改时间和访问时间. 2个目录项分别是'.', '..'
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {  //若不成功则放回目录的i节点,返回无空间出错码
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
//为该新i节点申请一用于保存目录项数据的磁盘块,用于保存目录项结构信息. 并令i节点的第一个直接块指针等于该块号. 如果申请失败则放回对应目录
//的i节点; 复位新申请的i节点连接计数; 放回该新的i节点,返回没有空间出错码退出. 否则置该新的i节点已修改标志.
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
//从设备上读取新申请的磁盘块(目的是把对应块放到高速缓冲区中), 若出错,则放回对应目录的i节点; 释放申请的磁盘块; 复位新申请的i节点连接计数
//放回该新的i节点,返回没有空间出错码退出
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
//然后在缓冲块中建立起所创建目录文件中的2个默认的新目录项'.','..'结构数据. 首先令de指向存放目录项的数据块. 然后置该目录项的i节点号字段
//等于新申请的i节点号, 名字字段等于'.', 然后de指向下一个目录项结构, 并在该结构中存放上级目录的i节点号和名字'..', 然后设置该高速缓冲块已
//修改标志, 并释放该缓冲块. 再初始化设置新i节点的模式字段,并置该i节点已修改标志.
	de = (struct dir_entry *) dir_block->b_data;
	de->inode=inode->i_num;
	strcpy(de->name,".");  //设置'.'目录项
	de++;
	de->inode = dir->i_num;
	strcpy(de->name,".."); //设置'..'目录项
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
//在指定目录中新添加一个目录项,用于存放新建目录的i节点号和目录名. 如果失败(包含该目录项的高速缓冲区指针=NULL),则放回目录的i节点,所申请的
//i节点引用连接计数复位,并放回该i节点,返回出错码
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
//最后令该新目录项的i节点字段等于新i节点号,并置高速缓冲块已修改标志, 放回目录和新的i节点,释放高速缓冲块,最后返回0(成功)
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir) //检测指定的目录是否为空的子程序
 */
//检测指定目录是否空; inode 指定目录的i节点指针; 返回: 1目录中是空的; 0 不空
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;
//计算指定目录中现有目录项个数并检查开始两个特定目录项中信息是否正确. 一个目录中应该起码有2个目录项: 即'.', '..' 如果目录项个数少于
//2个或该目录i节点的第1个直接块没有指向任何磁盘块号,或者该直接块读不出,则显示警告信息, 返回0(失败)
	len = inode->i_size / sizeof (struct dir_entry); //目录中目录项个数
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
//此时bh所指缓冲块中含有目录项数据. 让目录项指针de指向缓冲块中第1个目录项. 对于第1个目录项'.', 它的i节点号字段inode应该等于当前目录的
//i节点号. 对于第2个目录项'..', 它的i节点号字段inode应该等于上一层目录的i节点号,不会为0. 因此如果第1个目录项的i节点号字段值!=该目录
//的i节点号,或者第2个目录项的i节点号字段为0,或者两个目录项的名字字段不分别的等于'.', '..', 则显示出错信息,并返回0
	de = (struct dir_entry *) bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode || 
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
//令nr等于目录项序号(从0开始); de指向第3个目录项,并循环检测该目录中其余所有的(len-2)个目录项,看有没有目录项的i节点号字段不为0(被使用)
	nr = 2;
	de += 2;
	while (nr<len) {
//如果该磁盘块中的目录项已全部检测完毕,则释放该磁盘块的缓冲块,并读取目录数据文件中下一块含有目录项的磁盘块. 读取方法是根据当前检测的目录序列
//号nr计算出对应目录项在目录数据文件中的数据块号(nr/DIR_ENTRIES_ER_BLOCK),然后使用bmap()取得对应的盘块号block, 再使用读设备盘块函数bread()
//把相应盘块读入缓冲块中, 并返回该缓冲块的指针, 若读取的相应盘块没有使用(或已经不用,如文件以删除)则继续读下一块,若读不出,则出错返回0.
//否则让de指向读出块的首个目录项.
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))
				return 0;
			de = (struct dir_entry *) bh->b_data;
		}
//对于de指向的当前目录项,如果该目录项的i节点字段不等于0, 则表示该目录项目前正被使用,则释放该高速缓冲区,返回0退出. 否则若还没有查询完该
//目录中的所有目录项,则把该目录项序号nr +1, de指向下一个目录项, 继续检测.
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh); //执行到这里,说明该目录中没有找到已用的目录项(除了头两个以外), 则释放缓冲块返回1
	return 1;
}
//删除目录; name目录名(路径名) 返回0表示成功,否则返回出错码
int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
//检测操作许可和参数的有效性并取路径名中顶层目录的i节点. 如果不是超级用,则返回访问许可出错码; 如果找不到对应路径名中顶层目录的i节点,返回
//出错码; 如果最顶端的文件名长度为0, 则说明给出的路径名最后没有指定目录名,放回该目录i节点, 返回出错码; 如果在该目录中没有写的权限,则放回
//该目录的i节点, 返回访问许可出错码; 如果不是超级用户,则返回访问许可出错码.
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
//根据指定目录的i节点和目录名利用函数find_entry()寻找对应目录项,并返回包含该目录项的缓冲块指针bh,包含该目录项的目录的i节点指针dir和
//该目录项指针de. 再根据该目录项de中的i节点号利用iget()得到对应的i节点inode, 如果对应路径名上最后目录名的目录项不存在, 则释放包含该
//目录项的高速缓冲区, 放回目录的i节点,返回文件已存在出错码. 如果取目录项的i节点出错,则放回目录的i节点,并释放含有目录项的高速缓冲区,返回出错码
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
//此时已有包含要被删除目录项的目录i节点dir,要被删除目录项的i节点inode和要被删除目录项指针de, 下面通过对这3个对象中信息的检查来验证删除
//操作的可行性. 若该目录设置了受限删除标志且进程的有效用户id不是root,且进程的有效用户id不等于该i节点的用户id, 则表示当前进程没有删除
//该目录权限,于是放回包含要删除目录名的目录i节点和该要删除目录的i节点, 然后释放高速缓冲区,返回出错码
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
//如果要被删除的目录项i节点的设备号!=包含该目录项的目录的设备号,或者该被删除目录的引用连接计数>1(表示有符号连接)则不能删除该目录,于是
//释放包含要删除目录名的目录i节点和该要删除目录的i节点,释放高速缓冲块,返回出错码
	if (inode->i_dev != dir->i_dev || inode->i_count>1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
//如果要被删除目录的目录项i节点就等于包含该需删除目录的目录i节点,则表示试图删除'.'目录, 不允许. 于是放回包含要删除目录名的目录i节点和
//要删除目录的i节点. 释放高速缓冲块,返回出错吗
	if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */
		iput(inode);  //不可以删除'.', 但可以删除'../dir'
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
//若要被删除目录i节点的属性表明这不是一个目录,则本删除操作的前提完全不存在, 于是放回包含删除目录名的目录i节点和该要删除目录的i节点,
//释放高速缓冲块,返回出错码
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
//若该需要被删除的目录不空,也不能删除,于是放回包含要删除目录名的目录i节点和该要删除目录的i节点, 释放高速缓冲块,返回出错码	
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
//对于一个空目录,其目录项连接数应该=2, 若该需被删除目录的i节点的连接数!=2, 则显示经过信息. 但删除操作仍然继续执行. 于是置该需被删除目录
//的目录项的i节点号字段=0, 表示该目录项不再使用, 并置含有该目录项的高速缓冲块已修改标志,并释放该缓冲块. 然后再置被删除目录i节点的连接数
//=0(表示空闲)置i节点已修改标志
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks=0;
	inode->i_dirt=1;
//再将包含被删除目录名的目录的i节点链接计数 -1, 修改其改变时间和修改时间为当前时间, 并置该节点已修改标志. 最后放回包含要删除目录名的目录
//i节点和该要删除目录的i节点, 返回0(删除操作成功)
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt=1;
	iput(dir);
	iput(inode);
	return 0;
}
//删除(释放)文件名对应的目录项; 从文件系统删除一个名字. 如果是文件的最后一个连接,且没有进程正在打开该文件, 则该文件也将被删除,并释放所占用
//的设备空间; name 文件名(路径名); 成功则返回0, 否则返回出错号
int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
//检测参数的有效性并取路径名中顶层目录的i节点. 如果找不到对应路径名中顶层目录的i节点, 则返回出错码; 如果最顶层的文件名长度为0, 则说明
//给出的路径名没有指定文件名,放回该目录i节点,返回出错码; 如果在该目录中没有写权限,则放回该目录的i节点 返回访问许可处出错码. 如果找不到对应
//路径名顶层目录的i节点,则返回出错码
	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
//根据指定目录的i节点和目录名利用函数寻找对应目录项,并返回包含该目录项的缓冲块指针bf,包含该目录项的目录的i节点指针dir和该目录项指针de
//再根据该目录项de中的i节点号利用iget()得到对应的i节点inode. 如果对应路径名上最后目录名的目录项不存在, 则释放包含该目录项的高速缓冲区
//放回目录的i节点,返回文件已经存在出错码并退出.如果取目录项的i节点出错, 则放回目录的i节点,并释放含有目录项的高速缓冲区,返回出错码
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
//此时已有包含要被删除目录项的目录i节点dir,要被删除目录项的i节点inode和要被删除目录项指针de, 下面通过对这3个对象中信息的检测来验证
//删除操作的可行性. 若该目录设置了受限删除标志且进程的euid不是root,且进程的euid != 该i节点的uid, 且进程的euid!=目录i节点的uid,
//则表示当前进程没有权限删除该目录, 于是放回包含要生成目录名的目录i节点和该要删除目录的i节点,然后释放高速缓冲块,返回出错码
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
//如果该指定文件名是一个目录, 也不能删除,放回该目录i节点和该文件名目录项的i节点,释放包含该目录项的缓冲块,返回出错码
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) { //如果该i节点的连接计数值=0,则显示警告, 并修正其=1
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
//现在可以删除文件名对应的目录项了,于是将该文件名目录项中的i节点号字段置0,表示释放该目录项, 并设置包含该目录项的缓冲块已修改标志,
//释放该高速缓冲块	
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
//然后把文件名对应i节点的连接数 -1, 置已修改标志, 更新改变时间为当前时间. 最后放回该i节点和目录的i节点,返回0(成功). 如果是文件的
//最后一个连接,即i节点连接数-1后=0, 且此时没有进程正打开该文件,那么在调用iput()放回i节点时, 该文件也将被删除,并释放所占用的设备空间
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}
//为文件建立一个文件名目录项, 为一个已存在的文件创建一个新连接(硬连接-hard link); oldname原路径名, newname 新的路径名
//成功则返回0, 否则返回出错号
int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;
//对原文件名进行又有效性验证,它应该存在并且不是一个目录名. 所以先取原文件路径名对应的i节点oldinode, 如果=0则表示出错,返回出错码;
//如果原路径名对应的是一个目录名,则放回该i节点,也返回出错码
	oldinode=namei(oldname);
	if (!oldinode)
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
//查找新路径名的最顶层目录的i节点dir,并返回最后的文件名及其长度. 如果目录的i节点没有找到,则放回原路径名的i节点,返回出错码. 如果新路径名
//中不包括文件名,则放回原路径名i节点和新路径名目录的i节点,返回出错码	
	dir = dir_namei(newname,&namelen,&basename);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
//不能跨设备建立硬连接, 因此如果新路径名顶层目录的设备号与原路径名的设备号不一样,则放回新路径名目录的i节点和原路径名的i节点,返回出错码;
//如果用户没有在新目录中写的权限,则也不能建立连接,于是放回新路径名目录的i节点和原路径名的i节点,返回出错码	
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
//查询该新路径名是否已存在, 如果存在则也不能建立连接.于是释放包含该存在目录项的高速缓冲块,放回新路径名目录的i节点和原路径名的i节点,返回
//出错号
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
//所有条件都满足了,于是在新目录中添加一个目录项,若失败则放回该目录的i节点和原路径名的i节点, 返回出错号. 否则初始设置该目录项的i节点号
//等于原路径名的i节点号,并置包含该新添目录项的缓冲块已修改标志,释放该缓冲块,放回目录的i节点	
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
//再将原节点的连接计数+1, 修改其改变时间为当前时间,并设置i节点已修改标号自己,最后放回原路径名的i节点,并返回0(成功)
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
