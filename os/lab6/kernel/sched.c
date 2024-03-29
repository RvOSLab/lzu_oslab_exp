/**
 * @file sched.c
 * @brief 实现绝大部分和进程有关的函数
 */
#include <assert.h>
#include <clock.h>
#include <errno.h>
#include <kdebug.h>
#include <mm.h>
#include <riscv.h>
#include <sched.h>
#include <string.h>
#include <trap.h>
extern void boot_stack_top(void); /** 启动阶段内核堆栈最高地址处 */

static void fcfs_schedule();
static void rr_schedule();
static void priority_schedule();
static void feedback_schedule();

/** 进程 0 */
union task_union init_task;

/** 当前进程进程控制块 */
struct task_struct* current = NULL;

/** 系统所有进程的进程控制块指针数组 */
struct task_struct* tasks[NR_TASKS];

/** 用户栈最大 20M */
uint64_t stack_size = 20 * (1 << 20); 

/**
 * @brief 将进程处理器状态 context 压入进程内核堆栈
 *
 * @param stack 内核堆栈指针
 * @param context 进程处理器状态
 * @return 堆栈上的 context 结构体指针
 */
context* push_context(char* stack, context* context)
{
    char* new_stack = stack - sizeof(struct trapframe);
    *(struct trapframe*)new_stack = *context;
    return (struct trapframe*)new_stack;
}

/**
 * @brief 保存当前进程的处理器状态
 *
 * 只在中断中保存处理器状态，因此保存的是中断发生后的状态
 * @param context 处理器状态指针
 */
void save_context(context* context)
{
    current->context = *context;
}

/**
 * @brief 初始化进程模块
 *
 * 主要负责初始化进程 0
 */
void sched_init()
{
    tasks[0] = (struct task_struct*)&init_task;
    for (size_t i = 1; i < NR_TASKS; ++i) {
        tasks[i] = NULL;
    }
    init_task.task = (struct task_struct) {
        .state = TASK_RUNNING,
        .counter = 15,
        .priority = 15,
        .start_time = ticks /* 0 */,
        // .start_code = START_CODE,
        // .start_stack = START_STACK,
        // .start_kernel = START_KERNEL,
        // .start_rodata = (uint64_t)&rodata_start - (0xC0200000 - 0x00010000),
        .start_data = (uint64_t)data_start - ((uint64_t)kernel_start - START_CODE),
        .end_data = (uint64_t)kernel_end - ((uint64_t)kernel_start - START_CODE),
        .brk = (uint64_t)kernel_end - ((uint64_t)kernel_start - START_CODE),
        .pg_dir = pg_dir,
        .swap_info.clock_info.vaddr = START_CODE,
    };

    current = &init_task.task;

    schedule_queue_init();
}

/**
 * @brief 切换进程
 *
 * 保存当前进程处理器状态 `context`，切换到进程号为 `task` 的进程。
 *
 * 本函数仅实现进程切换，发生进程切换时，进程从此函数切换到别的进程，
 * 恢复时返回到本函数并直接退出，不做多余的事情。
 *
 * caller-saved registers 会在返回后由调用者恢复，callee-saved registers
 * 会在本函数返回时恢复。因此，只要只需要存储必要的信息，确保退出函数栈帧
 * 时能够恢复 callee-saved registers 即可，不需要保存全部通用寄存器。
 *
 * @return 目标进程的处理器状态指针
 */
void switch_to(size_t task)
{
    if (current == tasks[task]) {
        return;
    }

    uint64_t is_disable = read_csr(sstatus) & SSTATUS_SIE;
    disable_interrupt();
    register uint64_t t0 asm("t0");
    write_csr(sscratch, t0);
    t0 = (uint64_t)&current->context;
    __asm__ __volatile__ (
            "sd zero, 0(%0)\n\t"
            "sd ra, 8(%0)\n\t"
            "sd sp, 16(%0)\n\t"
            "sd gp, 24(%0)\n\t"
            "sd tp, 32(%0)\n\t"
            "sd t1, 48(%0)\n\t"
            "mv t1, t0\n\t"
            "mv t0, zero\n\t"
            "csrrw t0, sscratch, t0\n\t"
            "sd t0, 40(t1)\n\t"
            "mv t0, t1\n\t"
            "sd t2, 56(%0)\n\t"
            "sd s0, 64(%0)\n\t"
            "sd s1, 72(%0)\n\t"
            "sd a0, 80(%0)\n\t"
            "sd a1, 88(%0)\n\t"
            "sd a2, 96(%0)\n\t"
            "sd a3, 104(%0)\n\t"
            "sd a4, 112(%0)\n\t"
            "sd a5, 120(%0)\n\t"
            "sd a6, 128(%0)\n\t"
            "sd a7, 136(%0)\n\t"
            "sd s2, 144(%0)\n\t"
            "sd s3, 152(%0)\n\t"
            "sd s4, 160(%0)\n\t"
            "sd s5, 168(%0)\n\t"
            "sd s6, 176(%0)\n\t"
            "sd s7, 184(%0)\n\t"
            "sd s8, 192(%0)\n\t"
            "sd s9, 200(%0)\n\t"
            "sd s10, 208(%0)\n\t"
            "sd s11, 216(%0)\n\t"
            "sd t3, 224(%0)\n\t"
            "sd t4, 232(%0)\n\t"
            "sd t5, 240(%0)\n\t"
            "sd t6, 248(%0)\n\t"
            "csrr t1, sstatus\n\t"
            "sd t1, 256(%0)\n\t"
            "csrr t1, stval\n\t"
            "sd t1, 272(%0)\n\t"
            "csrr t1, scause\n\t"
            "sd t1, 280(%0)\n\t"
            : /* empty output list */
            : "r" (t0)
            : "memory", "t1"
            );
    current->context.status |= SSTATUS_SPP; /* 确保切换后处理器处于 S-mode */
    current->context.epc = (uint64_t)&&ret; /* 返回后直接退出函数 */

    current = tasks[task];
    pg_dir = current->pg_dir;
    active_mapping();
    char* stack;

    /* 用户态：内核堆栈为空 */
    if (current->context.gpr.sp < START_KERNEL) {
        stack  = (char*)current + PAGE_SIZE;
    } else { /* 内核态：堆栈不为空 */
        stack = (char*)current->context.gpr.sp;
    }
    __trapret(push_context(stack, &current->context));
ret:
    set_csr(sstatus, is_disable);
    return;
}

/**
 * @brief 进程调度函数
 *
 * 使用公平调度算法(CFS)。
 * 进程 0 不参加调度，当且仅当只有进程 0 时选择进程 0
 *
 * @return 目标进程的进程号
 */
void schedule()
{
    rr_schedule();
}

static void fcfs_schedule()
{
    if(current){
        if(!(current->state == TASK_RUNNING) || (current->pid == 0)){
            struct task_struct *next_process = pop_first_process();
            uint64_t next = next_process ? next_process->pid : 0; // 队列中无进程可调时才调度进程 0
            kprintf("switch to %u\n", next);
            switch_to(next);
        } else {
            current->counter = 4294967295;
        }
    }
}

static void rr_schedule()
{
    if (current){
        if (current->state == TASK_RUNNING && current->pid){
            push_process_to_schedule_queue(current); // 将当前进程放入队尾
        }
    }
    struct task_struct *next_process = pop_first_process();
    uint64_t next = next_process ? next_process->pid : 0; // 队列中无进程可调时才调度进程 0
    kprintf("switch to %u\n", next);
    switch_to(next);
}

static void priority_schedule()
{
    if (current)
    {
        if (current->state == TASK_RUNNING)
        {
            /* 若是正常运行被调出，则重设时间片 */
            current->counter = (current->priority) + 1;
            if (current->pid)
                push_process_to_schedule_queue(current); // 将当前进程放入队尾
        }
    }

    struct task_struct *next_process = NULL;
    for (uint64_t prio = 0; prio < 16; prio++)
    {
        if ((next_process = pop_priority_process(prio)))
            break;
    }

    uint64_t next = next_process ? next_process->pid : 0; // 还是没有找到合适进程，队列中无进程可调时才调度进程 0
    kprintf("switch to %u with priority %u\n", next, next_process ? next_process->priority : init_task.task.priority);
    switch_to(next);
}

static void feedback_schedule()
{
    if (current)
    {
        if (current->state == TASK_RUNNING)
        {
            /* 若是正常运行被调出，则降低当前进程优先级，重设时间片 */
            if (current->priority < 16)
            {
                ++current->priority;
                current->counter = (current->priority) + 1;
            }
            if (current->pid)
            {
                push_process_to_schedule_queue(current); // 将当前进程放入队尾
            }
        }
    }

    struct task_struct *next_process = NULL;
    /* 第一轮查找 */
    for (uint64_t prio = 0; prio < 16; prio++)
    {
        if ((next_process = pop_priority_process(prio)))
            break;
    }

    /* 所有可运行的进程都（在最低优先级）耗尽了时间片 */
    if (!next_process)
    {
        /* 所有进程恢复最高优先级与时间片 */
        kprintf("all processes are running out of time slices.\nresuming time slices and set to priority 0...\n");
        for (uint64_t i = 0; i < NR_TASKS; i++)
        {
            if (tasks[i])
            {
                tasks[i]->priority = 0;
                tasks[i]->counter = 1 + tasks[i]->priority;
            }
        }
        /* 再次查找进程 */
        for (uint64_t prio = 0; prio < 16; prio++)
        {
            if ((next_process = pop_priority_process(prio)))
                break;
        }
    }

    uint64_t next = next_process ? next_process->pid : 0; // 还是没有找到合适进程，队列中无进程可调时才调度进程 0
    kprintf("switch to %u with priority %u\n", next, next_process ? next_process->priority : init_task.task.priority);
    switch_to(next);
}


/**
 * @brief 把current任务置为可中断/不可中断的睡眠状态，并让睡眠队列头指针指向当前任务。
 * 
 * @param p 等待任务队列头指针
 * @param state 睡眠状态，TASK_UNINTERRUPTIBLE 或 TASK_INTERRUPTIBLE
 * TASK_UNINTERRUPTIBLE状态的进程需要内核程序利用 wake_up()函数明确唤醒
 * TASK_INTERRUPTIBLE的任务可以通过信号、任务超时等唤醒（置为TASK_RUNNING）
*/
static inline void __sleep_on(struct task_struct **p, uint32_t state)
{
	struct task_struct *tmp;

	if (!p) {
		return;
	}
	if (current == &(init_task.task)) {
		panic("task[0] trying to sleep");
	}
	tmp = *p;
	*p = current;
	current->state = state;
    delete_process_from_schedule_queue(current);
repeat:	schedule();
	if (*p && *p != current) {
		(**p).state = TASK_RUNNING;
		current->state = TASK_UNINTERRUPTIBLE;
		goto repeat;
	}
	if (!*p) {
		kputs("Warning: *P = NULL\n\r");
	}
	if ((*p = tmp)) {
		tmp->state = TASK_RUNNING;
	}
}

void interruptible_sleep_on(struct task_struct **p)
{
	__sleep_on(p, TASK_INTERRUPTIBLE);
}

void sleep_on(struct task_struct **p)
{
	__sleep_on(p, TASK_UNINTERRUPTIBLE);
}

/**
 * 唤醒不可中断等待任务
 * @param 		p 		任务结构指针
 * @return		void
 */
void wake_up(struct task_struct **p)
{
	if (p && *p) {
		if ((**p).state == TASK_STOPPED) {
			kputs("wake_up: TASK_STOPPED");
		}
		if ((**p).state == TASK_ZOMBIE) {
			kputs("wake_up: TASK_ZOMBIE");
		}
		(**p).state = TASK_RUNNING;
        push_process_to_schedule_queue(*p);
	}
}

/**
 * @brief 创建进程 0 各段的映射
 *
 * @param start 虚拟地址起始
 * @param end 虚拟地址结束
 * @param flag 权限位
 */
static void map_segment(uint64_t start, uint64_t end, uint16_t flag)
{
    uint64_t physical = PHYSICAL(start);
    start -= VIRTUAL(SBI_END) - START_CODE;
    end -= VIRTUAL(SBI_END) - START_CODE;
    while (start != end) {
        put_page(physical, start, flag);
        start += PAGE_SIZE;
        ++mem_map[MAP_NR(physical)];
        physical += PAGE_SIZE;
    }
}

/**
 * @brief 初始化进程 0
 *
 * 创建用户态进程零，将内核移入进程中。
 * @see init_task0()
 * @note 只有在发生中断时才能保存处理器状态，
 *       因此让 sys_init() 占据系统调用号 0,
 *       但是实际上 sys_init() 被内核调用，并
 *       不是系统调用。
 */
int64_t sys_init(struct trapframe* tf)
{
    /* 创建指向内核代码/数据的用户态映射 */
    map_segment((uint64_t)text_start, (uint64_t)rodata_start, USER_RX | PAGE_VALID);
    map_segment((uint64_t)rodata_start, (uint64_t)data_start, USER_R | PAGE_VALID);
    map_segment((uint64_t)data_start, (uint64_t)kernel_end, USER_RW | PAGE_VALID);

    /*
     * 创建进程 0 堆栈（从 0xBFFFFFF0 开始）
     *
     * 内核堆栈已使用的空间远小于一页，因此仅为用户态堆栈分配一页虚拟页。
     * 为了确保切换到应用态后正确执行，将内核态堆栈的数据全部拷贝到用用户态堆栈中。
     */
    get_empty_page(START_STACK - PAGE_SIZE, USER_RW);
    get_empty_page(START_STACK, USER_RW);
    invalidate();
    memcpy((void*)((uint64_t)START_STACK - PAGE_SIZE), (const void*)FLOOR(tf->gpr.sp), PAGE_SIZE);
    tf->gpr.sp += START_STACK - (uint64_t)boot_stack_top;
    /* GCC 使用 s0 指向函数栈帧起始地址（高地址），因此这里也要修改，否则切换到进程0会访问到内核区 */
    tf->gpr.s0 += START_STACK - (uint64_t)boot_stack_top;
    save_context(tf);
    return 0;
}

/**
 * @brief 进程退出
 *
 * 退出进程号为 task 的进程
 * @see kill()
 */

void exit_process(size_t task, uint32_t exit_code)
{
    for (int32_t i = NR_TASKS - 1; i > -1; i--)
        if (tasks[i] && tasks[i]->pid == task)
        {
            tasks[i]->state = TASK_ZOMBIE;
            tasks[i]->exit_code = exit_code;
            kprintf("process %u exited.\n", task);
            schedule(); // 最后让内核重新调度任务运行。
        }
}

void do_exit(uint32_t exit_code)
{
    exit_process(current->pid, exit_code);
}

/**
 * @brief 设置进程优先级
 *
 * 暂时只支持 which 为 PRIO_PROCESS，且没有root用户权限检测
 */
int64_t do_setpriority(int64_t which, int64_t who, int64_t niceval)
{
    if (which == PRIO_PROCESS)
    {
        if (who == 0)
        {
            current->priority = niceval;
            kprintf("set the priority of process %u to %u.\n", current->pid, current->priority);
            return 0;
        }
        else
        {
            for (uint64_t i = 0; i < NR_TASKS; i++)
            {
                if (tasks[i] && tasks[i]->pid == who)
                {
                    tasks[i]->priority = niceval;
                    kprintf("set the priority of process %u to %u.\n", tasks[i]->pid, tasks[i]->priority);
                    return 0;
                }
            }
        }
    }
    return -1;
}
