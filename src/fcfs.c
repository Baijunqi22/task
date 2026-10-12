/* ============================================================================
 *  fcfs.c —— 先来先服务（First Come First Served）调度算法模拟
 *
 *  算法思想：
 *    1. 按进程到达时间的先后次序建立就绪队列（到达时间相同则按输入顺序）；
 *    2. 处理机每次把 CPU 交给队首进程，该进程一直运行到完毕才释放 CPU，
 *       因此 FCFS 是一种非抢占式调度算法；
 *    3. 若队首进程尚未到达，处理机处于空闲状态，直到它到达为止。
 *
 *  衡量指标：周转时间 = 完成时间 - 到达时间
 *            带权周转时间 = 周转时间 / 服务时间
 *            等待时间 = 周转时间 - 服务时间
 *
 *  用法: fcfs [-f 输入文件] [-h]
 * ==========================================================================*/
#include "sched.h"

static const char *USAGE =
    "用法: fcfs [-f 输入文件] [-h]\n"
    "  先来先服务（FCFS）调度算法模拟。\n"
    "  -f <文件>  从文件读入进程，每行格式：名称 到达时间 服务时间\n"
    "  -h         显示本帮助\n"
    "不带 -f 时从键盘交互输入。\n";

int main(int argc, char **argv)
{
    Proc  p[MAX_PROC];
    const char *file;
    int   n, clock = 0;

    os_lab_init_console();

    if (has_opt(argc, argv, "-h") || has_opt(argc, argv, "--help")) {
        fputs(USAGE, stdout);
        return 0;
    }
    warn_unknown_opts(argc, argv, "-f");

    file = opt_value(argc, argv, "-f");
    n = file ? proc_load_file(file, p, MAX_PROC) : proc_prompt(p, MAX_PROC, 0);
    if (n <= 0) return 1;

    title_line("先来先服务（FCFS）调度算法");
    printf("调度规则：按到达时间先后依次执行，非抢占，进程一旦占用处理机便运行至完毕。\n");
    proc_print_input(p, n);

    proc_sort_by_arrival(p, n);      /* 就绪队列：到达早的在前 */
    proc_reset_stats(p, n);

    /* ---------------- 模拟调度过程 ---------------- */
    gantt_begin();
    for (int i = 0; i < n; i++) {
        if (clock < p[i].arrive) {          /* 处理机空闲，等待下一个进程到达 */
            gantt_seg(NULL, clock, p[i].arrive);
            clock = p[i].arrive;
        }
        p[i].first_start  = clock;
        p[i].finish       = clock + p[i].burst;
        p[i].turnaround   = p[i].finish - p[i].arrive;
        p[i].w_turnaround = (double)p[i].turnaround / p[i].burst;
        p[i].wait         = p[i].turnaround - p[i].burst;
        gantt_seg(p[i].name, p[i].first_start, p[i].finish);
        clock = p[i].finish;
    }
    gantt_end();

    /* ---------------- 输出结果 ---------------- */
    printf("\n调度顺序（就绪队列次序）：");
    for (int i = 0; i < n; i++)
        printf("%s%s", i ? " -> " : "", p[i].name);
    printf("\n最后完成时刻 = %d\n", clock);

    proc_print_stats(p, n, "各进程调度结果");

    title_line("结果分析提示");
    printf("· FCFS 对长作业有利、对短作业不利：排在长作业之后的短作业等待时间明显偏长；\n");
    printf("· 若出现处理机空闲时间段，说明 FCFS 未利用空闲时间先执行后续已到达的进程；\n");
    printf("· FCFS 不会产生饥饿，但平均周转时间通常大于 SJF/时间片轮转等算法。\n");
    return 0;
}
