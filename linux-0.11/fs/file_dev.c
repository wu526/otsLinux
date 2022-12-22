/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
//文件读函数, 根据i节点和文件结构,读取文件中数据. 由i节点可以知道设备号,由filep可以知道文件
//中当前读写指针位置. buf指定用户空间中缓冲区的位置, count是需要读取的字节数. 返回值是实际读取
//的字节数,或出错号(<0)
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;
//判断参数的有效性. 若需要读取的字节计数count<=0,则返回0; 若还需要读取的字节数!=0, 就循环执行
//直到数据全部读出或遇到问题. 在循环读中,根据i节点和文件表结构信息,利用bmap()得到包含文件当前
//读写位置的数据块在设备上对应的逻辑块号nr. 若nr!=0,则从i节点指定的设备上读取该逻辑块.
//如果读操作失败则退出循环, 若nr=0,表示指定的数据块不存在,缓冲块指针=NULL, (filp->f_pos)/BLOCK_SIZE
//用于计算出文件当前指针所在数据块号
	if ((left=count)<=0)
		return 0;
	while (left) {
		if (nr = bmap(inode,(filp->f_pos)/BLOCK_SIZE)) {
			if (!(bh=bread(inode->i_dev,nr)))
				break;
		} else
			bh = NULL;
//计算文件读写指针在数据块中的偏移值nr, 则在该数据块中希望读取的字节数(BLOCK_SIZE-nr),
//然后和现在还需读取的字节数left作比较, 其中小值即为本次操作需要读取的字节数chars. 如果
//BLOCK_SIZE-nr > left, 说明该块是需要读取的最后一块数据, 反之则还需要读取下一块数据.
//之后调整读写文件指针, 指针前移此次将读取的字节数chars, 剩余字节计数left减去chars
		nr = filp->f_pos % BLOCK_SIZE;
		chars = MIN( BLOCK_SIZE-nr , left );
		filp->f_pos += chars;
		left -= chars;
//从上面设备上读到了数据, 将p指向缓冲块中开始读取数据的位置, 并复制chars字节到用户缓冲区buf
//否则往用户缓冲区中填入chars个0值字节		
		if (bh) {
			char * p = nr + bh->b_data;
			while (chars-->0)
				put_fs_byte(*(p++),buf++);
			brelse(bh);
		} else {
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
	}
//修改该i节点的访问时间为当前时间,返回读取的字节数,若读取字节数=0,则返回出错号.
	inode->i_atime = CURRENT_TIME;
	return (count-left)?(count-left):-ERROR;
}
//文件写函数, 根据i节点和文件结构信息,将用户数据写入文件中; 由i节点可以知道设备号,由file
//结构可以知道文件中当前读写指针位置. buf指定用户态中缓冲区的位置, count为需要写入的字节
//数. 返回值是实际写入的字节数,或出错码(<0)
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block,c;
	struct buffer_head * bh;
	char * p;
	int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
//确定数据写入文件的位置,如果是要项文件后添加数据,则将文件读写指针移动到文件尾部,否则就将在文件当前读写指针处写入
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;
//在以写入字节数i(刚开始=0)<指定写入字节数count时,循环执行; 循环中,先取文件数据块号(pos/BLOCK_SIZE)
//在设备上对应的逻辑块号block, 如果对应的逻辑块不存在就创建一块. 如果得到的逻辑块号=0, 则表示
//创建失败, 于是退出循环. 否则根据该逻辑块号读取设备上的相应逻辑块,若出错也退出循环.
	while (i<count) {
		if (!(block = create_block(inode,pos/BLOCK_SIZE)))
			break;
		if (!(bh=bread(inode->i_dev,block)))
			break;
//缓冲块指针bh正指向刚读入的文件数据块,现在再求出文件当前读写指针在该数据块中的偏移值c,并将
//指针p指向缓冲块中开始写入数据的位置,并置该缓冲块已修改标志.对于块中当前指针,从开始读写位置
//到块末共可写入c=(BLOCK_SIZE-c)个字节. 若c>剩余还需写入的字节数(count-i),则此次只写入c=(count-i)个字节
		c = pos % BLOCK_SIZE;
		p = c + bh->b_data;
		bh->b_dirt = 1;
		c = BLOCK_SIZE-c;
		if (c > count-i) c = count-i;
//写入数据前,先预设下一次循环操作要读写文件中的位置. 因此把pos指针前移此次需要写入的字节数.
//如果此时pos位置超过了文件当前长度,则修改i节点中文件长度字段,并置i节点已修改标志. 然后把
//此次要写入的字节数c累加到已写入字节计数值i中,供循环判断使用. 接着从用户缓冲区buf中复制c
//个字节到高速缓冲块中p指向的开始位置处,复制完后就释放该缓冲块
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		i += c;
		while (c-->0)
			*(p++) = get_fs_byte(buf++);
		brelse(bh);
	}
//数据全部写入文件或在写操作过程中发生问题时就退出循环. 此时更改文件修改时间为当前时间,并
//调整文件读写指针. 如果此次操作不是在文件尾添加数据,则把文件读写指针调整到当前读写位置pos
//处,并更改文件i节点的修改时间为当前时间, 最后返回写入的字节数.若写入字节数=0,则返回出错号-1
	inode->i_mtime = CURRENT_TIME;
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	return (i?i:-1);
}
