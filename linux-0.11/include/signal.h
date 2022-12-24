#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h> //类型 头文件, 定义了基本的系统数据类型

typedef int sig_atomic_t; //定义信号原子操作类型
typedef unsigned int sigset_t;		/* 32 bits */ //定义信号集类型

#define _NSIG             32 //定义信号种类 - 32种
#define NSIG		_NSIG // NSIG = _NSIG
//以下这些是Linux0.11内核中定义的信号, 其中包括了POSIX.1 要求的所有20个信号
#define SIGHUP		 1  // Hang up 挂断控制终端或进程
#define SIGINT		 2 // Interrupt 来自键盘的中断
#define SIGQUIT		 3 // Quit 来自键盘的退出
#define SIGILL		 4 //Illeagle 非法指令
#define SIGTRAP		 5 //Trap 跟踪断点
#define SIGABRT		 6 //Abort 异常结束
#define SIGIOT		 6 //IO trap 同上
#define SIGUNUSED	 7 //Unused 没有使用
#define SIGFPE		 8 //FPE 协处理出错
#define SIGKILL		 9 //Kill 强迫进程终止
#define SIGUSR1		10 //User1 用户信号1, 进程可使用
#define SIGSEGV		11 //Segment violation 无效内存引用
#define SIGUSR2		12 //User2 用户信号2, 进程可使用
#define SIGPIPE		13 //Pipe 管道写出错,无读者
#define SIGALRM		14 //Alarm 实时定时器报警
#define SIGTERM		15 //Terminate 进程终止
#define SIGSTKFLT	16 //Stack Fault 栈出错(协处理器)
#define SIGCHLD		17 //Child 子进程停止或被终止
#define SIGCONT		18 // Continue 恢复进程继续执行
#define SIGSTOP		19 //Stop 停止进程的执行
#define SIGTSTP		20 // TTY Stop tty发出停止进程, 可忽略
#define SIGTTIN		21 // TTY In 后台进程请求输入
#define SIGTTOU		22 // TTY Out 后台进程请求输出

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */ //linux0.11中已经实现了sigaction()
#define SA_NOCLDSTOP	1  // 当子进程处于停止状态, 就不对SIGCHLD处理
#define SA_NOMASK	0x40000000 //不阻止在指定的信号处理程序中再收到该信号
#define SA_ONESHOT	0x80000000 //信号句柄一旦被调用过就恢复到默认处理句柄
//以下常量用于 sigprocmask(hwo,) 改变阻塞信号集(屏蔽码), 用于改变该函数的行为
#define SIG_BLOCK          0	/* for blocking signals */ //在阻塞信号集中加上给定信号  line37
#define SIG_UNBLOCK        1	/* for unblocking signals */ //从阻塞信号集中删除指定信号
#define SIG_SETMASK        2	/* for setting the signal mask */ //设置阻塞信号集 //line39
//以下两个常数符号都表示指向无返回值的函数指针,且都有一个int整形参数. 这两个指针值是逻辑上将实际上不可能出现的函数地址值. 可作为下面
//signal函数的第二个参数. 用于告知内核,让内核处理信号或忽略对信号的处理.
#define SIG_DFL		((void (*)(int))0)	/* default signal handling */ //默认信号处理程序(信号句柄)
#define SIG_IGN		((void (*)(int))1)	/* ignore signal */ //忽略信号的处理程序
//sigaction的数据结构. sa_handler 是对应某信号指定要采取的行动. 可以用上面的SIG_DFL或SIG_IGN来忽略该信号,也可以是指向处理该信号
//函数的一个指针. sa_mask 给出了信号的屏蔽码, 在信号程序执行时将阻塞对这些信号的处理; sa_flags 指定改变信号处理过程的信号集.是由
//line37~line39的位标志定义的; sa_restorer是恢复函数指针,由库函数libc提供, 用于清理用户态堆栈.
//另外,引起触发信号处理的信号也将被阻塞, 除非使用了SA_NOMASK标志
struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
//signal函数用于是为信号_sig安装一新的信号处理程序(信号句柄), 与sigaction()类似. 该函数含有两个参数: 指定需要捕获的信号_sig;
//具有一个参数且无返回值的函数指针_func; 该函数返回值也是具有一个int参数(最后一个int)且无返回值的函数指针, 他是处理该信号的原处理句柄
void (*signal(int _sig, void (*_func)(int)))(int);
//下面两函数用于发送信号, kill()用于向任何进程或进程组发送信号. raise()用于向当前进程自身发送信号. 其作用等价于kill(getpid(), sig)
int raise(int sig);
int kill(pid_t pid, int sig);
//在进程的任务结构中, 除有一个以比特位表示当前进程待处理的32位信号字段signal外, 还有一个同样以比特位表示的用于屏蔽进程当前阻塞信号集(
//屏蔽信号集)的字段blocked, 也是32位. 每个比特代表一个对应的阻塞信号. 修改进程的屏蔽信号集可以阻塞或解除阻塞所指定的信号. 以下五个函数就是
//用于操作进程屏蔽信号集. 本版本内核还未实现; sigaddset(), sigdelset()用于对信号集中的信号进行增、删修改. sigaddset()用于
//向mask指向的信号集中增加指定的信号signo. sigdelset()则反之. sigemptyset(),sigfillset()用于初始化进程屏蔽信号集. 每个程序在使用
//信号集前,都需要使用这两个函数之一对屏蔽信号即进行初始化. sigempty()用于清空屏蔽的所有信号,即响应所有的信号; sigfillset()向信号集中
//置入所有信号,即屏蔽所有信号. SIGINT,SIGSTO是不能被屏蔽的. sigismember()用于测试一个指定信号是否在信号集中(1是, 0否, -1出错)
int sigaddset(sigset_t *mask, int signo);
int sigdelset(sigset_t *mask, int signo);
int sigemptyset(sigset_t *mask);
int sigfillset(sigset_t *mask);
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
//对set中的信号进行检测,看是否有挂起的信号.在set中返回进程中当前被阻塞的信号集
int sigpending(sigset_t *set);
//用于改变进程目前被阻塞的信号集(信号屏蔽码), 若oldset不是NULL,则通过其返回进程当前屏蔽信号集. 所set!=NULL,则根据how指示修改进程
//屏蔽信号集
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
//用sigmask临时替换进程的信号屏蔽码,然后暂停该进程直到收到一个信号.若捕捉到某一信号并从该信号处理程序中返回,则该函数也返回.且信号屏蔽码会
//恢复到调用前的值
int sigsuspend(sigset_t *sigmask);
//用于改变进程在收到指定信号时所采取的行动. 即改变信号的处理句柄
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
