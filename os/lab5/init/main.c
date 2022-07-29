#include <sbi.h>
#include <kdebug.h>
#include <mm.h>
#include <trap.h>
#include <sched.h>
#include <clock.h>
#include <syscall.h>
#include <lib/sleep.h>

int main()
{
    kputs("\nLZU OS STARTING....................");
    print_system_infomation();
    mem_init();
    mem_test();
    malloc_test();
    set_stvec();
    sched_init();
    clock_init();
    kputs("Hello LZU OS");
    enable_interrupt();
    usleep_queue_init();

    init_task0();
    syscall(NR_fork);    /* task 0 creates task 1 */
    syscall(NR_fork);    /* task 0 creates task 2, task 1 creates task 3 */
    long pid = syscall(NR_getpid);
    syscall(NR_test_fork, pid);

    // fcfs test
    /**
    if (syscall(NR_getpid) == 1)
        syscall(NR_exit);
    if (syscall(NR_getpid) == 2)
        syscall(NR_exit);
    */

    // rr & feedback test
    /**
    if (syscall(NR_getpid) == 1)
    {
        syscall(NR_setpriority, PRIO_PROCESS, 0, 10);
        syscall(NR_setpriority, PRIO_PROCESS, 2, 1);
    }
    */
    
    // priority test
    /**
    if (syscall(NR_getpid) == 1)
    {
        syscall(NR_setpriority, PRIO_PROCESS, 0, 3);
        syscall(NR_setpriority, PRIO_PROCESS, 2, 1);
        syscall(NR_setpriority, PRIO_PROCESS, 3, 1);
    }

    if (syscall(NR_getpid) == 2){
        for (uint64_t i = 0; i < 30000000; i++)
        {
            uint64_t j = 3 * i;
        }
        syscall(NR_exit);
    }

    if (syscall(NR_getpid) == 3){
        for (uint64_t i = 0; i < 50000000; i++)
        {
            uint64_t j = 3 * i;
        }
        syscall(NR_exit);
    }
    */
   if (pid) {
        uint64_t sleep_time = 0;
        printf("[main@pid = %u] sleep time(us) > ", pid);
        scanf("%u", &sleep_time);
        syscall(NR_usleep, sleep_time);
        printf("[main@pid = %u] wake after %u us\n", pid, sleep_time);
   }

    while (1)
        ; /* infinite loop */
    return 0;
}
