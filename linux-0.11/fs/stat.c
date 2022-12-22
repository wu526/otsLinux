/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
//复制文件状态信息; indoe:文件i节点; statbuf 用户数据空间中 stat 文件状态结构指针, 用于存放取得的状态信息
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	int i;
//验证(或分配)存放数据的内存空间, 然后临时复制相应节点上的信息
	verify_area(statbuf,sizeof (* statbuf));
	tmp.st_dev = inode->i_dev;  //文件所在的设备号
	tmp.st_ino = inode->i_num; // 文件i节点号
	tmp.st_mode = inode->i_mode; // 文件属性
	tmp.st_nlink = inode->i_nlinks;  // 文件的连接数
	tmp.st_uid = inode->i_uid;  // 文件的用户id
	tmp.st_gid = inode->i_gid; //文件的组id
	tmp.st_rdev = inode->i_zone[0]; // 设备号(若是特殊字符文件或块设备文件)
	tmp.st_size = inode->i_size; //文件字节长度(如果文件是常规文件)
	tmp.st_atime = inode->i_atime; // 最后访问时间
	tmp.st_mtime = inode->i_mtime; //最后修改时间
	tmp.st_ctime = inode->i_ctime;  // 最后i节点修改时间
	for (i=0 ; i<sizeof (tmp) ; i++) //将这些状态信息复制到用户缓冲区中
		put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}
//文件状态系统调用; 根据给定的文件名获取相关文件状态信息; filename:指定的文件名; stabuf: 存放状态信息的缓冲区指针;
//成功返回0, 出错返回出错码
int sys_stat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;
//根据文件名找出对应的i节点, 然后将i节点上的文件状态信息复制到用户缓冲区中,并放回该i节点
	if (!(inode=namei(filename)))
		return -ENOENT;
	cp_stat(inode,statbuf);
	iput(inode);
	return 0;
}
//文件状态系统调用; 根据给定的文件句柄获取相关文件状态信息; fd:指定文件的句柄, statbuf: 存放状态信息的缓冲区指针;
//成功返回0, 出错返回出错码
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct m_inode * inode;
//取文件句柄对应的文件结构,然后从中得到文件的i节点,然后将i节点上的文件状态信息复制到用户缓冲区. 如果文件句柄>一个程序最多打开文件数
//NR_OPEN或该句柄的文件结构指针为空或对应文件结构的i节点字段为空,则出错. 返回出错码并退出
	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	cp_stat(inode,statbuf);
	return 0;
}
