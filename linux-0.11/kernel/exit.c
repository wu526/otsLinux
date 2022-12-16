/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void); // 将进程置为睡眠状态, 直到收到信号
int sys_close(int fd);  // 关闭指定文件的系统调用
/*释放指定进程占用的任务槽及其任务数据结构占用的内存页面. 参数p是任务数据结构指针, 该函数
在后面的sys_kill(),sys_waitpid()中被调用. 扫描任务指针数组 task[]以寻找指定的任务.
如果找到,则首先清空该任务槽,然后释放该任务数据结构所占用的内存页面, 最后执行调度函数并在
返回时立即退出. 如果在任务数组表中没有找到指定任务对应的项, 则内核panic
*/
void release(struct task_struct * p)
{
	int i;

	if (!p) // 进程数据结构指针是NULL, 则什么也不做, 退出
		return;
	for (i=1 ; i<NR_TASKS ; i++)  // 扫描任务数组, 寻找指定任务
		if (task[i]==p) {
			task[i]=NULL;  // 置空该任务项并释放相关内存页
			free_page((long)p);
			schedule();  // 重新调度(TODO:似乎没必要??)
			return;
		}
	panic("trying to release non-existent task");  // 指定任务不存在则死机
}
/* 向指定任务p发送信号 sig, 权限=priv
sig: 信号值; p: 指定任务的指针; priv: 强制发送信号的标志. 即不需要考虑进程用户属性或级别
而能发送信号的权力. 该函数首先判断参数的正确性, 然后判断条件是否满足. 如果满足就向指定进程
发送信号sig并退出, 否则返回未许可错误号
*/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)  // 若信号不正确或任务指针为空则出错退出
		return -EINVAL;
// 如果强制发送标志置位, 或当前进程的有效用户标识符(euid)就是指定进程的euid(即自己),
// 或者当前进程是超级用户, 则向进程p发送信号sig,即在进程p位图中添加该信号,否则出错退出.
// suser()定义为 current->euid==0, 用于判断是否是超级用户
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}
// 终止会话(session)
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task; // *p先指向任务数组最末端
// 扫描任务指针数组, 对于所有的任务(除任务0), 如果其会话号 session 等于当前进程的会话号
// 就向它发送挂断进程信号 SIGHUP
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);  // 发送挂断进程信号
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 * 为了向进程组发送信号, XXX需要检查许可
 */
/*系统调用kill()可用于向任何进程或进程组发送任何信号, 而并非只是杀死进程
pid: 进程号; sig: 需要发送的信号
pid>0, 则信号被发送给进程号是pid的进程; pid=0,信号就会被发送给当前进程的进程组中的所有进程
pid=-1, 信号sig就会发送给除第一个进程(init进程)外的所有进程
pid<-1, 则信号sig将发送给进程组-pid的所有进程
sig=0, 则不发送信号, 但仍会进行错误检查, 如果成功则返回0
该函数扫描任务数组表, 并根据pid的值对满足条件的进程发送指定的信号sig. 若pid=0, 则表明当前
进程是进程组组长, 因此需要向所有组内的进程强制发送信号 sig.
*/
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if (err=send_sig(sig,*p,1))  // 强制发送信号
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if (err=send_sig(sig,*p,0))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK)
		if (err = send_sig(sig,*p,0))
			retval = err;
	else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if (err = send_sig(sig,*p,0))
				retval = err;
	return retval;
}
// 通知父进程: 向父进程pid发送信号SIGCHLD, 默认情况下子进程将停止或终止. 如果没有找到父进程
// 则自己释放, 但根据POSIX.1要求, 若父进程已先行终止, 则子进程应该被初始进程1收容
static void tell_father(int pid)
{
	int i;

	if (pid)
// 扫描进程数组, 寻找指定进程pid, 并向其发送子进程将停止或终止信号 SIGCHLD	
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current); // 没有找到父进程, 则自己释放
}
// 程序退出处理函数, 被后面的sys_exit()调用. 该函数将把当前进程设置为 TASK_ZOMBIE状态
// 然后直线调度函数schedule(), 不再返回. code是退出状态码或错误码
int do_exit(long code)
{
	int i;
// 首先释放当前进程代码段和数据段所占的内存页. free_page_tables()的第一个参数(get_base()返回值)
// 指明在cpu线性地址空间中起始基地址, 第2个参数(get_limit()返回值)说明欲释放的字节长度值
// get_base()宏中的 current->ldt[1] 给出进程代码段描述符的位置, current->ldt[2]给出进程
// 数据段描述符的位置. get_limit()中的0x0f是进程代码段的选择符(0x17是数据段的选择符).
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
// 如果当前进程有子进程, 就将子进程的father 设置为1(其父进程改为进程1, init进程)
// 如果该子进程已处于僵死状态, 则向进程1发送子进程终止信号SIGCHLD.
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init, 这里假设task[1]肯定是进程 init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
// 关闭当前进程打开的所有文件		
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
// 对当前进程的工作目录pwd, 根目录root以及执行文件的i节点进行同步操作, 放回各个i节点并分别置空(释放)
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
// 如果当前进程是会话头领(leader)进程且其有控制终端, 则释放该终端.
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
// 如果当前进程上次使用过协处理器, 则将 last_task_used_math 置空		
	if (last_task_used_math == current)
		last_task_used_math = NULL;
// 如果当前进程是leader 进程, 则终止该会话的所有相关进程		
	if (current->leader)
		kill_session();
// 把当前进程置为僵死状态, 表明当前进程已经释放了资源. 并保存将由父进程读取的退出码		
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
// 通知父进程, 即向父进程发送信号 SIGCHLD, 子进程将停止或终止	
	tell_father(current->father);
	schedule(); // 重新调度进程运行, 让父进程处理僵死进程其他的善后事宜.
// return 语句仅用于去掉警告信息. 因为这个函数不返回, 所以若在函数名前加关键字volatile
// 就可以告诉gcc编译器本函数不会返回的特殊情况. 这样可让gcc产生更好一些的代码, 且可以不用
// 再写这条 return 语句也不会产生假警告信息	
	return (-1);	/* just to suppress warnings */
}

// 系统调用 exit(), 终止进程
// error_code是用户程序提供的退出状态信息, 只有低字节有效. 把error_code 左移8bit是wait()
// waitpid()函数的要求, 低字节中将用来保存wait()的状态信息. 例如: 如果进程处于暂停状态
// 那么其低字节就等于 0x7f. wait(), waitpid()利用这些宏就可以取得子进程的退出码或子进程终止的原因(信号)
int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}
// 系统调用waitpid(), 挂起当前进程, 直到 pid 指定的子进程退出或收到要求终止该进程的信号,
// 或者是需要调用一个信号句柄(信号处理程序). 如果pid所指的子进程已退出(已成为僵死进程),
// 则本调用将立刻返回, 子进程使用的所有资源将释放
// pid>0, 表示等待进程号等于pid的子进程; pid=0,表示等待进程组号等于当前进程组号的任何子进程
// pid<-1,等待进程组号等于pid绝对值的任何子进程; pid=-1 等待任何子进程;
// options=WUNTRACED, 如果子进程是停止的, 也马上返回(无须跟踪)
// options=WNOHNAGE, 如果没有子进程退出或终止就马上返回.
// 如果返回状态指针 stat_addr 不为空, 则就将状态信息保存到那里
// 参数 pid 是进程号; *stat_addr 是保存状态信息位置的指针, options 是waitpid 选项
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code; // flag 用于后面表示所选出的子进程处于就绪或睡眠状态
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
// 任务数组末端开始扫描所有任务, 跳过空项、本进程项以及非当前进程的子进程项	
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current)
			continue;
		if ((*p)->father != current->pid)
			continue;
// 此时扫描选择到进程p肯定是当前进程的子进程, 如果等待的子进程号pid>0,但与被扫描子进程p
// 的pid不相等, 说明它是当前进程另外的子进程, 于是跳过该进程, 接着扫描下一个进程
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
// 否则如果指定等待进程的pid=0, 表示正在等待进程组号等于当前进程组号的任何子进程.如果此时
// 被扫描进程 p 的进程组号与当前进程的组号不等, 则跳过				
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
// 否则如果指定的pid<-1, 表示正在等待进程组号等于pid绝对值的任何子进程. 如果此时被扫描
// 进程p的组号与pid的绝对值不等, 则跳过.				
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
// 前面对pid的判断都不符号, 则表示当前进程正在等待其任何子进程, 即pid=-1的情况, 此时所选择
// 到的进程p或是其进程号等于指定pid, 或者当前进程组中的任意子进程, 或者是进程号等于指定pid
// 绝对值的子进程, 或者是任何子进程(此时指定的pid=-1), 接下来根据子进程p所处的状态来处理
		switch ((*p)->state) {
// p 处于停止状态, 如果此时 WUNTRACED 没有置位, 表示程序无需立刻返回, 于是继续扫描处理其他进程
// WUNTRACED置位, 则把状态信息 0x7f 放入*stat_addr, 并立刻返回子进程号 pid. 0x7f 表示的返回状态
// 使WIFSTOPPED()宏为真.			
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
// p处于僵死状态, 则首先把它再用户态和内核态运行的时间分别累计到当前进程(父进程)中,
// 然后取出子进程的pid和退出码, 并释放该子进程, 最后返回子进程的退出码和pid
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid; // 临时保存子进程pid
				code = (*p)->exit_code; // 取子进程的退出码
				release(*p);  // 释放该子进程
				put_fs_long(code,stat_addr);  // 置状态信息为退出码
				return flag;  // 返回子进程的pid
// 如果这个子进程p的状态既不是停止也不是僵死, 就置flag=1, 表示找到过一个符合要求的子进程,
// 但它处于运行态或睡眠态				
			default:
				flag=1;
				continue;
		}
	}
// 在上面对任务数组扫描结束后, 如果flag被置位, 说明由复合等待要求的子进程并没有处于退出或僵死
// 状态, 如果此时已设置WNOHANG选项(表示若没有子进程处于退出或终止态就立即返回)就立刻返回0,退出
// 否则把当前进程置为可中断等待状态并重新执行调度. 当又开始执行本进程时, 如果本进程没有收到除
// SIGCHLD 以外的信号, 则还是重复处理. 否则返回出错码"中断的系统调用"并退出. 针对这个出错
// 号用户程序应该再继续调用本函数等待子进程.
	if (flag) {
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;  // 返回出错码(中断的系统调用)
	}
	return -ECHILD; // 若没有找到符号要求的子进程, 则返回出错码(子进程不存在)
}