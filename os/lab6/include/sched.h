/**
 * @file sched.h
 * @brief 声明进程相关的数据类型和宏
 *
 * 进程拥有 4G 虚拟地址空间，并被划分成多个段，布局如下：
 * 0xFFFFFFFF----->+--------------+
 *                 |              |
 *                 |              |
 *                 |    Kernel    |
 *                 |              |
 *                 |              |
 * 0xC0000000----->---------------+
 *                 |    Hole      |
 * 0xBFFFFFF0----->---------------+
 * (START_STACK)   |    Stack     |
 *                 |      +       |
 *                 |      |       |
 *                 |      v       |
 *                 |              |
 *        brk----->+--------------+
 *                 |      ^       |
 *                 |      |       |
 *                 |      +       |
 *                 | Dynamic data |
 *   end_data----->+--------------+
 *                 |              |
 *                 |    .bss      |
 *                 |    .data     |
 *                 |              |
 * start_data----->+--------------+
 *                 |              |
 *                 |    .rodata   |
 *                 |    .text     |
 *                 |              |
 * 0x00010000----->---------------+
 *                 |   Reserved   |
 * 0x00000000----->+--------------+
 *
 * 段与段之间不一定是相连的，中间可能存在空洞。
 */
#ifndef __SCHED_H__
#define __SCHED_H__
#include <mm.h>
#include <trap.h>
#include <riscv.h>
#include <kdebug.h>
#include <utils/schedule_queue.h>
#include <kdebug.h> // for print_schedule_queue()

#define NR_TASKS             512                              /**< 系统最大进程数 */

#define LAST_TASK tasks[NR_TASKS - 1]                         /**< tasks[] 数组最后一项 */
#define FIRST_TASK tasks[0]                                   /**< tasks[] 数组第一项 */

/// @{ @name 进程状态
#define TASK_RUNNING         0                                /**< 正在运行 */
#define TASK_INTERRUPTIBLE   1                                /**< 可中断阻塞 */
#define TASK_UNINTERRUPTIBLE 2                                /**< 不可中断阻塞 */
#define TASK_ZOMBIE          3                                /**< 僵尸状态（进程已终止，但未被父进程回收）*/
#define TASK_STOPPED         4                                /**< 进程停止 */
/// @}

/// @{ 进程内存布局
#define START_CODE 0x10000                                    /**< 代码段起始地址 */
#define START_STACK 0xBFFFFFF0                                /**< 堆起始地址（最高地址处） */
#define START_KERNEL 0xC0000000                               /**< 内核区起始地址 */
/// @}

/// @{ 优先级设定的 which 种类
#define PRIO_PROCESS    0   //进程
#define PRIO_PGRP   1   //进程组
#define PRIO_USER   2   //用户进程
/// @}

typedef struct trapframe context;                             /**< 处理器上下文 */

/** 进程控制块 PCB(Process Control Block) */
struct task_struct {
    uint32_t uid;  /* 用户ID */
    uint32_t euid; /* 有效用户ID */
    uint32_t suid; /* 保存的设置用户id */
    uint32_t gid;  /* 组id */
    uint32_t egid; /* 有效组id */
    uint32_t sgid; /* 保存的设置组id */

    uint32_t exit_code;           /**< 返回码 */
    uint32_t pid;                 /**< 进程 ID */
    uint32_t pgid;                /**< 进程组 */
    // uint64_t start_code;          /**< 代码段起始地址 */
    // uint64_t start_rodata;        /**< 只读数据段起始地址 */
    uint64_t start_data;          /**< 数据段起始地址 */
    uint64_t end_data;            /**< 数据段结束地址 */
    uint64_t brk;                 /**< 堆结束地址 */
    // uint64_t start_stack;         /**< 栈起始地址 */
    // uint64_t start_kernel;        /**< 内核区起始地址 */
    uint32_t state;               /**< 进程调度状态 */
    uint32_t counter;             /**< 时间片大小 */
    uint32_t priority;            /**< 进程优先级 */
    struct vfs_inode *fd[4];
    struct task_struct *p_pptr;   /**< 父进程 */
    struct task_struct *p_cptr;   /**< 子进程 */
    struct task_struct *p_ysptr;  /**< 创建时间最晚的兄弟进程 */
    struct task_struct *p_osptr;  /**< 创建时间最早的兄弟进程 */
    uint32_t utime,stime;         /**< 用户态、内核态耗时 */
    uint32_t cutime,cstime;       /**< 进程及其子进程内核、用户态总耗时 */
    size_t start_time;            /**< 进程创建的时间 */
    uint64_t *pg_dir;             /**< 页目录地址 */
    union {
        struct {
            uint64_t vaddr;         // Clock 调度算法中上次搜到了哪个虚拟地址
        } clock_info;
    } swap_info;
    context context;              /**< 处理器状态，请把此成员放在 PCB 的最后 */
};

/**
 * @brief 初始化进程 0
 * 手动进入中断处理，调用 sys_init() 初始化进程0
 * @see sys_init()
 * @note 这个宏使用了以下三个 GNU C 拓展：
 *       - [Locally Declared Labels](https://gcc.gnu.org/onlinedocs/gcc/Local-Labels.html)
 *       - [Labels as Values](https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html)
 *       - [Statements and Declarations in Expressions](https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html#Statement-Exprs)
 */
#define init_task0()                                                        \
({                                                                          \
    __label__ ret;                                                          \
    write_csr(scause, CAUSE_USER_ECALL);                                    \
    clear_csr(sstatus, SSTATUS_SPP);                                        \
    set_csr(sstatus, SSTATUS_SPIE);                                         \
    clear_csr(sstatus, SSTATUS_SIE);                                        \
    write_csr(sepc, &&ret - 4 - (SBI_END + LINEAR_OFFSET - START_CODE));    \
    write_csr(sscratch, (char*)&init_task + PAGE_SIZE);                     \
    register uint64_t a7 asm("a7") = 0;                                     \
    __asm__ __volatile__("call __alltraps \n\t" ::"r"(a7):"memory");        \
    ret: ;                                                                  \
})

/**
 * @brief 进程数据结构占用的页
 *
 * 进程 PCB 和内核态堆栈共用一页。
 * PCB 处于一页的低地址，内核态堆栈从页最高地址到低地址增长。
 */
union task_union {
    struct task_struct task;                                  /**< 进程控制块 */
    char stack[PAGE_SIZE];                                    /**< 内核态堆栈 */
};

extern struct task_struct *current;
extern struct task_struct *tasks[NR_TASKS];
extern union task_union init_task;
extern uint64_t stack_size;

void sched_init();
void schedule();
void save_context(context *context);
context* push_context(char *stack, context *context);
void switch_to(size_t task);
void interruptible_sleep_on(struct task_struct **p);
void sleep_on(struct task_struct **p);
void wake_up(struct task_struct **p);
void exit_process(size_t task, uint32_t exit_code);
void do_exit(uint32_t exit_code);
int64_t do_setpriority(int64_t which,int64_t who,int64_t niceval);

/**
 * 打印当前调度队列
 */
static inline void print_schedule_queue()
{
    struct linked_list_node *node;
    for_each_linked_list_node(node, &schedule_queue.list_node)
    {
        struct schedule_queue_node *schedule_queue = container_of(node, struct schedule_queue_node, list_node);
        kprintf("%u->", schedule_queue->task->pid);
    }
    kprintf("\n");
}


/**
 * 弹出调度队列中第一个优先级为 n 的有时间片进程，没找到返回 NULL
 */
static inline struct task_struct *pop_priority_process(uint64_t prio)
{
    struct linked_list_node *node;
    struct task_struct *return_process = NULL;
    for_each_linked_list_node(node, &schedule_queue.list_node)
    {
        struct schedule_queue_node *cur_node = container_of(node, struct schedule_queue_node, list_node);
        if (cur_node->task->priority == prio && cur_node->task->counter > 0)
        {
            return_process = cur_node->task;
            linked_list_remove(node);
            kfree(cur_node);
            break;
        }
    }
    return return_process;
}

#endif /* end of include guard: __SCHED_H__ */
