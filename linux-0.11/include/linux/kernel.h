/*
 * 'kernel.h' contains some often-used function prototypes etc
 */
void verify_area(void * addr,int count); //验证给定地址开始的内存块是否超限, 若超限则追加内存.
volatile void panic(const char * str); //显示内核出错信息, 然后进入死循环
int printf(const char * fmt, ...); //标准打印(显示)函数
int printk(const char * fmt, ...); //内核专用的打印信息函数, 功能与printf()相同
int tty_write(unsigned ch,char * buf,int count); // 往tty上写指定长度的字符串
void * malloc(unsigned int size); // 通用内核内存分配函数
void free_s(void * obj, int size); // 释放指定对象占用的内存

#define free(x) free_s((x), 0)

/*
 * This is defined as a macro, but at some point this might become a
 * real subroutine that sets a flag if it returns true (to do
 * BSD-style accounting where the process is flagged if it uses root
 * privs).  The implication of this is that you should do normal
 * permissions checks first, and check suser() last.
 */
#define suser() (current->euid == 0) //检测是否为超级用户

