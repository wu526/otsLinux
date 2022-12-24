#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t; //从GMT1970.1.1午夜0时开始计的时间(秒)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#define CLOCKS_PER_SEC 100 //系统时钟滴答频率, 100HZ

typedef long clock_t; //从进程开始系统经过的时钟滴答数

struct tm {
	int tm_sec;  //秒[0,59]
	int tm_min; //分钟[0,59]
	int tm_hour; //小时[0,59]
	int tm_mday; //1个月的天数[0, 31]
	int tm_mon; //1年中月份[0,11]
	int tm_year; //从1900年开始的年数
	int tm_wday; //1星期中的某天[0,6](星期天=0)
	int tm_yday; //1年中的某天[0, 365]
	int tm_isdst;  //夏令时标志, 正数:使用; 0:没有使用; 负数:无效
};
//以下是有关时间操作的函数原型
clock_t clock(void); //确定处理器使用时间. 返回程序所用处理器时间(滴答数)的近似值
time_t time(time_t * tp); //取时间(秒数), 返回从1970.1.1 0:0:0开始的秒数(称为日历时间)
double difftime(time_t time2, time_t time1); //计算时间差,返回时间time2与time1之间经过的秒数
time_t mktime(struct tm * tp); // 将tm结构表示的时间转换成日历时间

char * asctime(const struct tm * tp); //将tm结构表示的时间转换成一个字符串,返回指向该串的指针
char * ctime(const time_t * tp); //将日历时间转换成一个字符串形式
struct tm * gmtime(const time_t *tp); // 将日历时间转换成tm结构表示的UTC时间(UTC-时间时间代码Universal time code)
struct tm *localtime(const time_t * tp); //将日历时间转换成tm结构表示的指定时区(Time zone)的时间
//将tm结构表示的时间利用格式字符串fmt转换成最大长度为smax的字符串并将结果存储在s中
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp); 
void tzset(void); //初始化时间转换信息,使用环境变量TZ, 读zname变量进行初始化; 在与时区相关的时间转换函数中将自动调用该函数

#endif
