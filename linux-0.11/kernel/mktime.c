/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60  // 1分钟的秒数
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)  # 1年的秒数

/* interestingly, we assume leap-years */
// 以年为界限, 定义了每个月开始的秒数时间
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

// 计算从1970年1月1日0时起到开机当日经过的秒数, 作为开机时间, 参数tm 中各字段以在 init/main.c 中被赋值, 信息取值 CMOS
long kernel_mktime(struct tm * tm)
{
	long res;
	int year;

/*
首先计算70年到现在经过的年数, 因为是2位表示方式, 所以会有2000年位图. 可以简单在最前面添加一条语句来解决这个问题:
if(tm->tm_year < 70) tm->tm_year += 100; 由于unix计年份y是从1970算起, 到1972年就是一个闰年, 因此过3年(71,72,73)就是第一个
润年, 这样从1970年开始的闰年数计算方法就应该为 1 + (y - 3)/4, 即(y+1)/4. 
res = 这些年经过的秒数 + 每个闰年多1天的秒数时间 + 当年到当月时间的秒数. 
month[]数组中已经在2月中包含了闰年时的天数, 因此若当年不是闰年且当前月份大于2月份的话, 就要减去这天. 又因为从70年开始算起, 所以当年
是闰年的判断方法是(y + 2) 能被4整除, 若不能整除就不是闰年
*/
	year = tm->tm_year - 70;
/* magic offsets (y+1) needed to get leapyears right.*/
	res = YEAR*year + DAY*((year+1)/4);  // 为了获得正确的闰年数, 需要一个魔幻值 y+1
	res += month[tm->tm_mon];
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
	if (tm->tm_mon>1 && ((year+2)%4))  // y+2如果不是闰年, 就必须减去一天的秒数
		res -= DAY;
	res += DAY*(tm->tm_mday-1);  // 加上本月过去的天数的秒数时间
	res += HOUR*tm->tm_hour;  // 加上当天过去的小时数的秒数时间
	res += MINUTE*tm->tm_min; // 加上1小时内过去的秒数
	res += tm->tm_sec;  // 加上1分钟内已过去的秒数
	return res;  // 1970年以来经过的秒数时间
}
