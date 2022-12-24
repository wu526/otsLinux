#ifndef _CONFIG_H
#define _CONFIG_H

/*
 * The root-device is no longer hard-coded. You can change the default
 * root-device by changing the line ROOT_DEV = XXX in boot/bootsect.s
 * 根文件系统设备已不是硬编码的了, 通过修改boot/bootsect.s中的ROOT_DEV=xxx可以改变根设备的默认设置值
 */

/*
 * define your keyboard here - 定义键盘类型
 * KBD_FINNISH for Finnish keyboards //芬兰键盘
 * KBD_US for US-type 美式键盘
 * KBD_GR for German keyboards 德式键盘
 * KBD_FR for Frech keyboard 法式键盘
 */
/*#define KBD_US */
/*#define KBD_GR */
/*#define KBD_FR */
#define KBD_FINNISH

/*
 * Normally, Linux can get the drive parameters from the BIOS at
 * startup, but if this for some unfathomable reason fails, you'd
 * be left stranded. For this case, you can define HD_TYPE, which
 * contains all necessary info on your harddisk.
 * 通常Linux能够在启动时从BIOS中获取驱动器的参数,但若由于未知原因而没有得到这些参数时,会使程序束手无策, 这种情况下,可以定义HD_TYPE
 * 其中包括硬盘的所有信息
 * The HD_TYPE macro should look like this:
 *HD_TYPE宏应该象下面这中形式
 * #define HD_TYPE { head, sect, cyl, wpcom, lzone, ctl}
 *
 * In case of two harddisks, the info should be sepatated by
 * commas:
 *对于有两个硬盘的情况, 参数信息需用逗号隔开
 * #define HD_TYPE { h,s,c,wpcom,lz,ctl },{ h,s,c,wpcom,lz,ctl }
 */
/*
 This is an example, two drives, first is type 2, second is type 3:
//这是一个例子, 两个硬盘, 第一个是类型2, 第2个是类型3
#define HD_TYPE { 4,17,615,300,615,8 }, { 6,17,615,300,615,0 }
对应所有硬盘, 若其磁头数<=8,则ctl=0; 磁头数>8,则ctl=8;
 NOTE: ctl is 0 for all drives with heads<=8, and ctl=8 for drives
 with more than 8 heads.
如果想让BIOS给出硬盘的类型,只需不定义HD_TYPE,这是默认操作
 If you want the BIOS to tell what kind of drive you have, just
 leave HD_TYPE undefined. This is the normal thing to do.
*/

#endif
