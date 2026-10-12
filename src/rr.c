/* ============================================================================
 *  rr.c —— 时间片轮转（Round Robin）调度算法模拟
 *
 *  算法思想：
 *    1. 所有就绪进程按到达的先后次序排成一个就绪队列（FIFO）；
 *    2. 处理机每次把 CPU 分配给队首进程，让它执行一个时间片（q 个时间单位）；
 *    3. 时间片用完而进程尚未完成，则它被剥夺 CPU 并排到就绪队列末尾；如此循环；
 *    4. 进程在时间片内完成则撤销，时间片未用完而不需要 CPU 时（本程序中为请求
 *       I/O）也交出处理机。
 *
 *  本程序采用的时间处理约定（同一时刻的事件先后次序）：
 *    ① I/O 完成被唤醒的进程入就绪队列；
 *    ② 新到达的进程入就绪队列；
 *    ③ 再处理当前进程的“完成 / 请求 I/O / 时间片用完重新排队”。
 *    因此同一时刻到达的新进程排在刚被剥夺 CPU 的进程之前。
 *
 *  用法: rr [-f 输入文件] [-t 时间片] [-h]
 * ==========================================================================*/
#include "sched.h"
#include <stdarg.h>

#define MAX_SEG   4096
#define MAX_EVENT 4096
#define DEFAULT_QUANTUM 2

typedef struct { char label[NAME_LEN + 8]; int start, end; } Seg;
typedef struct { int t; char text[160]; } Event;

/* 模拟过程的状态 */
typedef struct {
    Proc   *p;
    int     n;
    int     quantum;
    int     rem[MAX_PROC];         /* 尚需 CPU 时间 */
    int     cpu_used[MAX_PROC];    /* 已占用 CPU 时间 */
    int     io_done[MAX_PROC];     /* 是否已经完成 I/O */
    int     in_blocked[MAX_PROC];  /* 是否处于阻塞（I/O）状态 */
    int     wake[MAX_PROC];        /* I/O 完成时刻 */
    int     arrived_ptr;           /* 下一个将要到达的进程下标 */
    int     clock;
    int     done;                  /* 已完成进程数 */
    IntQueue ready, blocked;
} Sim;

static Seg   segs[MAX_SEG];
static int   nseg = 0;
static Event events[MAX_EVENT];
static int   nev = 0;

static void add_seg(const char *label, int start, int end)
{
    if (end <= start) return;
    if (nseg < MAX_SEG) {
        snprintf(segs[nseg].label, sizeof segs[nseg].label, "%s", label);
        segs[nseg].start = start;
        segs[nseg].end   = end;
    }
    nseg++;
    gantt_seg(label, start, end);
}

static void log_event(int t, const char *fmt, ...)
{
    va_list ap;

    if (nev >= MAX_EVENT) return;
    events[nev].t = t;
    va_start(ap, fmt);
    vsnprintf(events[nev].text, sizeof events[nev].text, fmt, ap);
    va_end(ap);
    nev++;
}

/* 取队列中第 k 个元素（0 表示队首），不改变队列内容 */
static int q_at(const IntQueue *q, int k)
{
    return q->a[(q->head + k) % IQ_CAP];
}

/* 处理 clock 时刻的事件：I/O 完成唤醒、新进程到达 */
static void process_events(Sim *s)
{
    int c = s->blocked.cnt;

    for (int k = 0; k < c; k++) {
        int i = iq_pop(&s->blocked);

        if (s->wake[i] <= s->clock) {
            s->in_blocked[i] = 0;
            iq_push(&s->ready, i);
            log_event(s->clock, "%s I/O 完成被唤醒，排入就绪队列队尾", s->p[i].name);
        } else {
            iq_push(&s->blocked, i);
        }
    }

    while (s->arrived_ptr < s->n && s->p[s->arrived_ptr].arrive <= s->clock) {
        int i = s->arrived_ptr++;
        iq_push(&s->ready, i);
        log_event(s->clock, "%s 到达（需占用 CPU %d），进入就绪队列队尾",
                  s->p[i].name, s->p[i].burst);
    }
}

/* 处理机空闲时，下一个可能引起状态变化的事件时刻 */
static int next_event_time(const Sim *s)
{
    int t = -1;

    if (s->arrived_ptr < s->n) t = s->p[s->arrived_ptr].arrive;
    for (int k = 0; k < s->blocked.cnt; k++) {
        int i = q_at(&s->blocked, k);
        if (t < 0 || s->wake[i] < t) t = s->wake[i];
    }
    return t;
}

static void simulate(Sim *s)
{
    process_events(s);                        /* 0 时刻先处理到达事件 */

    while (s->done < s->n) {
        int i, run, start, slice;

        if (iq_empty(&s->ready)) {            /* 就绪队列空：处理机空闲 */
            int nt = next_event_time(s);

            if (nt < 0) break;                /* 不会发生：还有进程未完成 */
            if (nt > s->clock) {
                add_seg("空闲", s->clock, nt);
                s->clock = nt;
            }
            process_events(s);
            continue;
        }

        i = iq_pop(&s->ready);
        if (s->p[i].first_start < 0) s->p[i].first_start = s->clock;

        slice = s->rem[i] < s->quantum ? s->rem[i] : s->quantum;
        run = slice;
        if (!s->io_done[i] && s->p[i].io_after > 0) {
            int left = s->p[i].io_after - s->cpu_used[i];   /* 距离请求 I/O 还差多少 */
            if (left < run) run = left;
        }
        if (run <= 0) run = 1;                /* 保护：正常流程不会出现 */

        start = s->clock;
        s->clock     += run;
        s->cpu_used[i] += run;
        s->rem[i]     -= run;
        add_seg(s->p[i].name, start, s->clock);

        process_events(s);                    /* ①② 先入队，③ 再处理当前进程 */

        if (s->rem[i] == 0) {
            s->p[i].finish       = s->clock;
            s->p[i].turnaround   = s->p[i].finish - s->p[i].arrive;
            s->p[i].w_turnaround = (double)s->p[i].turnaround / s->p[i].burst;
            s->p[i].wait         = s->p[i].turnaround - s->p[i].burst -
                                   (s->io_done[i] ? s->p[i].io_time : 0);
            s->done++;
            log_event(s->clock, "%s 运行完毕，撤销（周转时间 %d）",
                      s->p[i].name, s->p[i].turnaround);
        } else if (!s->io_done[i] && s->p[i].io_after > 0 &&
                   s->cpu_used[i] >= s->p[i].io_after) {
            s->io_done[i]   = 1;
            s->in_blocked[i] = 1;
            s->wake[i]      = s->clock + s->p[i].io_time;
            iq_push(&s->blocked, i);
            log_event(s->clock, "%s 请求 I/O（让出处理机），将于 %d 时刻完成 I/O",
                      s->p[i].name, s->wake[i]);
        } else {
            iq_push(&s->ready, i);
            log_event(s->clock, "%s 时间片用完（本次运行 %d 个时间单位），重新排到就绪队列队尾",
                      s->p[i].name, run);
        }
    }
}

static void print_events(void)
{
    title_line("调度过程事件时间线");
    cell("时刻", 8);
    cell("事件", 70);
    putchar('\n');
    for (int i = 0; i < nev; i++) {
        cellf(8, "%d", events[i].t);
        cell(events[i].text, 70);
        putchar('\n');
    }
}

static void print_segments(void)
{
    title_line("执行时间片段表");
    cell("起始时刻", 12);
    cell("结束时刻", 12);
    cell("占用处理机的进程", 22);
    putchar('\n');
    for (int i = 0; i < nseg && i < MAX_SEG; i++) {
        cellf(12, "%d", segs[i].start);
        cellf(12, "%d", segs[i].end);
        cell(segs[i].label, 22);
        putchar('\n');
    }
}

static const char *USAGE =
    "用法: rr [-f 输入文件] [-t 时间片] [-h]\n"
    "  时间片轮转（Round Robin）调度算法模拟。\n"
    "  -f <文件>  从文件读入进程，每行格式：名称 到达时间 服务时间 [I/O请求时间 I/O持续时间]\n"
    "  -t <时间片>  时间片长度（正整数，默认 2）\n"
    "  -h         显示本帮助\n";

int main(int argc, char **argv)
{
    Sim  s;
    Proc p[MAX_PROC];
    const char *file;
    int  n, quantum;

    os_lab_init_console();

    if (has_opt(argc, argv, "-h") || has_opt(argc, argv, "--help")) {
        fputs(USAGE, stdout);
        return 0;
    }
    warn_unknown_opts(argc, argv, "-f,-t");

    quantum = opt_int(argc, argv, "-t", DEFAULT_QUANTUM);
    if (quantum < 1) {
        fprintf(stderr, "错误：时间片必须为正整数。\n");
        return 1;
    }

    file = opt_value(argc, argv, "-f");
    n = file ? proc_load_file(file, p, MAX_PROC) : proc_prompt(p, MAX_PROC, 1);
    if (n <= 0) return 1;

    title_line("时间片轮转（Round Robin）调度算法");
    printf("调度规则：就绪进程按到达先后排成 FIFO 队列，队首进程运行一个时间片；\n"
           "          时间片用完未完成则排到队尾，如此循环。当前时间片 q = %d。\n", quantum);
    proc_print_input(p, n);

    proc_sort_by_arrival(p, n);
    proc_reset_stats(p, n);

    memset(&s, 0, sizeof s);
    s.p = p;
    s.n = n;
    s.quantum = quantum;
    iq_init(&s.ready);
    iq_init(&s.blocked);
    for (int i = 0; i < n; i++) s.rem[i] = p[i].burst;

    simulate(&s);

    gantt_end();
    print_segments();
    print_events();
    proc_print_stats(p, n, "各进程调度结果");

    title_line("结果分析提示");
    printf("· 时间片 q 越大，轮转次数越少，算法越接近 FCFS（对短作业不利）；\n");
    printf("· 时间片 q 越小，响应越快、进程间公平性越好，但进程切换开销也越大；\n");
    printf("· 时间片轮转的平均响应时间明显优于 FCFS，平均周转时间一般也较小。\n");
    return 0;
}
