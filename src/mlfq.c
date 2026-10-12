/* ============================================================================
 *  mlfq.c —— 多级反馈队列（Multi-Level Feedback Queue）调度算法模拟
 *
 *  算法思想（本程序实现的规则）：
 *    1. 设置 N 个就绪队列，优先级从第 1 级到第 N 级依次降低；
 *       第 i 级队列的时间片为 基准时间片 × 2^(i-1)（即逐级加倍）；
 *    2. 新到达的进程一律进入第 1 级队列队尾；
 *    3. 只有第 1 至第 i-1 级队列都为空时，才调度第 i 级队列中的进程；
 *       同一队列内部按时间片轮转（FIFO）调度；
 *    4. 进程用完了第 i 级队列的时间片仍未完成，则降入第 i+1 级队列队尾；
 *       在最低一级队列中不再降级，重新排到本队列队尾继续轮转；
 *    5. 若在运行过程中有更高优先级队列（级数更小）的进程到达，则立即抢占，
 *       被抢占的进程回到它当前所在队列的队尾，时间片重新计时；
 *    6. 进程因请求 I/O 而让出处理机，I/O 完成后再回到第 1 级队列（反馈）；
 *    7. 可选“老化”策略：每隔固定时间把所有就绪进程提升到第 1 级队列，
 *       防止长作业在低级队列中长时间得不到处理机（饥饿现象）。
 *
 *  同一时刻的事件处理次序：① I/O 完成唤醒 → ② 新进程到达 → ③ 老化提升
 *  → ④ 抢占判断 → ⑤ 处理当前进程的完成 / 请求 I/O / 时间片用完。
 *
 *  用法: mlfq [-f 输入文件] [-q 队列级数] [-t 基准时间片] [-a 老化周期] [-h]
 * ==========================================================================*/
#include "sched.h"
#include <stdarg.h>

#define MAX_SEG   8192
#define MAX_EVENT 8192

#define DEFAULT_LEVELS  3      /* 默认队列级数 */
#define DEFAULT_SLICE   1      /* 默认基准时间片 */
#define DEFAULT_AGING   0      /* 默认不启用老化 */

typedef struct { int t; char text[192]; } Event;
typedef struct { int pid, level, start, end; } Seg;   /* pid < 0 表示处理机空闲 */

typedef struct {
    Proc   *p;
    int     n;
    int     levels;                /* 队列级数 */
    int     base_slice;            /* 基准时间片 */
    int     aging;                 /* 老化周期，0 表示不启用 */
    int     level[MAX_PROC];       /* 进程当前所在队列（0 表示第 1 级） */
    int     rem[MAX_PROC];         /* 尚需 CPU 时间 */
    int     cpu_used[MAX_PROC];    /* 已占用 CPU 时间 */
    int     io_done[MAX_PROC];
    int     in_blocked[MAX_PROC];
    int     wake[MAX_PROC];
    int     arrived_ptr;
    int     clock;
    int     done;
    int     cur;                   /* 当前运行的进程，-1 表示空闲 */
    int     cur_slice_used;
    int     level_cpu[MAX_PROC][MAX_LEVEL];   /* 各进程在各队列上占用的时间 */
    int     queue_cpu[MAX_LEVEL];             /* 各队列累计占用的时间 */
    int     queue_dispatch[MAX_LEVEL];        /* 各队列被调度的次数 */
    int     wait_ticks[MAX_PROC];             /* 在就绪队列中等待的时间 */
    IntQueue q[MAX_LEVEL];
    IntQueue blocked;
} Sim;

static Seg   segs[MAX_SEG];
static int   nseg = 0;
static Event events[MAX_EVENT];
static int   nev = 0;

/* ---------------- 记录执行片段（供甘特图与片段表使用） ---------------- */
static int seg_pid = -1, seg_level = -2, seg_start = 0;

static void add_seg(const Sim *s, int pid, int level, int start, int end)
{
    char label[NAME_LEN + 16];

    if (end <= start) return;
    if (pid < 0) snprintf(label, sizeof label, "空闲");
    else         snprintf(label, sizeof label, "%s(Q%d)", s->p[pid].name, level + 1);

    if (nseg < MAX_SEG) {
        segs[nseg].pid   = pid;
        segs[nseg].level = level;
        segs[nseg].start = start;
        segs[nseg].end   = end;
    }
    nseg++;
    gantt_seg(label, start, end);
}

static void seg_sync(const Sim *s, int pid, int level)
{
    if (pid == seg_pid && level == seg_level) return;
    if (seg_level != -2) add_seg(s, seg_pid, seg_level, seg_start, s->clock);
    seg_pid   = pid;
    seg_level = level;
    seg_start = s->clock;
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

/* --------------------------------- 工具函数 -------------------------------- */
static int q_at(const IntQueue *q, int k)
{
    return q->a[(q->head + k) % IQ_CAP];
}

static int slice_of(const Sim *s, int level)
{
    return s->base_slice << level;         /* 基准时间片 × 2^level */
}

static int highest_ready(const Sim *s)
{
    for (int l = 0; l < s->levels; l++)
        if (!iq_empty(&s->q[l])) return l;
    return -1;
}

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

/* ------------------------------ 事件处理 ------------------------------ */
static void process_events(Sim *s)
{
    int c = s->blocked.cnt;

    /* ① I/O 完成：回到最高优先级队列（体现“反馈”） */
    for (int k = 0; k < c; k++) {
        int i = iq_pop(&s->blocked);

        if (s->wake[i] <= s->clock) {
            s->in_blocked[i] = 0;
            s->level[i] = 0;
            iq_push(&s->q[0], i);
            log_event(s->clock, "%s 的 I/O 完成，返回第 1 级队列队尾（反馈）", s->p[i].name);
        } else {
            iq_push(&s->blocked, i);
        }
    }

    /* ② 新到达的进程进入最高优先级队列 */
    while (s->arrived_ptr < s->n && s->p[s->arrived_ptr].arrive <= s->clock) {
        int i = s->arrived_ptr++;

        s->level[i] = 0;
        iq_push(&s->q[0], i);
        log_event(s->clock, "%s 到达（需占用 CPU %d），进入第 1 级队列队尾",
                  s->p[i].name, s->p[i].burst);
    }

    /* ③ 老化：把在低级队列中等待的进程提升到最高优先级队列 */
    if (s->aging > 0 && s->clock > 0 && s->clock % s->aging == 0) {
        int moved = 0;

        for (int l = s->levels - 1; l >= 1; l--) {
            while (!iq_empty(&s->q[l])) {
                int i = iq_pop(&s->q[l]);

                s->level[i] = 0;
                iq_push(&s->q[0], i);
                moved++;
                log_event(s->clock, "老化：%s 等待时间过长，被提升到第 1 级队列", s->p[i].name);
            }
        }
        (void)moved;
    }
}

/* -------------------------------- 模拟主过程 ------------------------------- */
static void simulate(Sim *s)
{
    process_events(s);                        /* 0 时刻的到达事件 */

    while (s->done < s->n) {
        int i, l;

        /* ④ 抢占判断：出现更高优先级队列的进程时，当前进程让出处理机 */
        if (s->cur >= 0) {
            int h = highest_ready(s);

            if (h >= 0 && h < s->level[s->cur]) {
                log_event(s->clock, "%s（第 %d 级队列）被更高优先级队列的进程抢占，回到本队列队尾",
                          s->p[s->cur].name, s->level[s->cur] + 1);
                iq_push(&s->q[s->level[s->cur]], s->cur);
                s->cur = -1;
            }
        }

        /* 选择应运行的进程 */
        if (s->cur < 0) {
            int h = highest_ready(s);

            if (h < 0) {                          /* 所有就绪队列为空 */
                int nt = next_event_time(s);

                if (nt < 0) break;                /* 正常不会发生 */
                if (nt > s->clock) {
                    seg_sync(s, -1, -1);          /* 处理机空闲 */
                    s->clock = nt;
                }
                process_events(s);
                continue;
            }

            s->cur = iq_pop(&s->q[h]);
            s->cur_slice_used = 0;
            s->queue_dispatch[h]++;
            if (s->p[s->cur].first_start < 0) s->p[s->cur].first_start = s->clock;
            log_event(s->clock, "%s 获得处理机：第 %d 级队列，时间片 %d",
                      s->p[s->cur].name, h + 1, slice_of(s, h));
        }

        /* 运行一个时间单位 */
        i = s->cur;
        l = s->level[i];
        seg_sync(s, i, l);          /* 本时间单位由该进程占用处理机（在时钟前进前记录） */
        s->clock++;
        s->cur_slice_used++;
        s->rem[i]--;
        s->cpu_used[i]++;
        s->level_cpu[i][l]++;
        s->queue_cpu[l]++;

        /* 统计刚刚过去的那一个时间单位内，处于就绪队列中的等待时间 */
        for (int j = 0; j < s->n; j++)
            if (j != s->cur && s->p[j].arrive < s->clock && s->rem[j] > 0 &&
                !s->in_blocked[j])
                s->wait_ticks[j]++;

        /* ① I/O 完成唤醒  ② 新到达进程入队  ③ 老化提升（都发生在当前时刻） */
        process_events(s);

        /* ⑤ 处理本次运行的结果 */
        if (s->rem[i] <= 0) {
            s->p[i].finish       = s->clock;
            s->p[i].turnaround   = s->p[i].finish - s->p[i].arrive;
            s->p[i].w_turnaround = (double)s->p[i].turnaround / s->p[i].burst;
            s->p[i].wait         = s->wait_ticks[i];
            s->cur = -1;
            s->done++;
            log_event(s->clock, "%s 运行完毕，撤销（周转时间 %d）",
                      s->p[i].name, s->p[i].turnaround);
        } else if (!s->io_done[i] && s->p[i].io_after > 0 &&
                   s->cpu_used[i] >= s->p[i].io_after) {
            s->io_done[i]    = 1;
            s->in_blocked[i] = 1;
            s->wake[i]       = s->clock + s->p[i].io_time;
            iq_push(&s->blocked, i);
            s->cur = -1;
            log_event(s->clock, "%s 请求 I/O 而让出处理机，将于 %d 时刻完成 I/O",
                      s->p[i].name, s->wake[i]);
        } else if (s->cur_slice_used >= slice_of(s, l)) {
            if (l < s->levels - 1) {              /* 降级 */
                s->level[i] = l + 1;
                iq_push(&s->q[l + 1], i);
                log_event(s->clock, "%s 用完第 %d 级队列的时间片（%d）仍未完成，降入第 %d 级队列队尾",
                          s->p[i].name, l + 1, slice_of(s, l), l + 2);
            } else {                              /* 最低级队列不再降级 */
                iq_push(&s->q[l], i);
                log_event(s->clock, "%s 用完最低一级队列的时间片仍未完成，重新排到本队列队尾",
                          s->p[i].name);
            }
            s->cur = -1;
        }

    }
    if (seg_level != -2) add_seg(s, seg_pid, seg_level, seg_start, s->clock);  /* 收尾 */
}

/* -------------------------------- 输出部分 ------------------------------- */
static void print_segments(const Sim *s)
{
    title_line("执行时间片段表（按处理机被占用的先后顺序）");
    cell("起始时刻", 12);
    cell("结束时刻", 12);
    cell("占用处理机的进程", 20);
    cell("所在队列", 12);
    putchar('\n');
    for (int i = 0; i < nseg && i < MAX_SEG; i++) {
        cellf(12, "%d", segs[i].start);
        cellf(12, "%d", segs[i].end);
        cell(segs[i].pid >= 0 ? s->p[segs[i].pid].name : "空闲", 20);
        if (segs[i].level >= 0) cellf(12, "第 %d 级", segs[i].level + 1);
        else                    cell("--", 12);
        putchar('\n');
    }
}

static void print_events(void)
{
    title_line("调度过程事件时间线");
    cell("时刻", 8);
    cell("事件", 78);
    putchar('\n');
    for (int i = 0; i < nev; i++) {
        cellf(8, "%d", events[i].t);
        cell(events[i].text, 78);
        putchar('\n');
    }
}

static void print_queue_stats(const Sim *s)
{
    title_line("各队列使用情况统计");
    cell("队列", 12);
    cell("时间片", 10);
    cell("被调度次数", 14);
    cell("累计运行时间", 16);
    putchar('\n');
    for (int l = 0; l < s->levels; l++) {
        char name[32];

        snprintf(name, sizeof name, "第 %d 级队列", l + 1);
        cell(name, 12);
        cellf(10, "%d", slice_of(s, l));
        cellf(14, "%d", s->queue_dispatch[l]);
        cellf(16, "%d", s->queue_cpu[l]);
        putchar('\n');
    }
}

static void print_per_level_usage(const Sim *s)
{
    title_line("各进程在各队列上获得处理机的时间");
    cell("进程名", 12);
    for (int l = 0; l < s->levels; l++) {
        char name[32];

        snprintf(name, sizeof name, "第%d级", l + 1);
        cell(name, 10);
    }
    putchar('\n');
    for (int i = 0; i < s->n; i++) {
        cell(s->p[i].name, 12);
        for (int l = 0; l < s->levels; l++) cellf(10, "%d", s->level_cpu[i][l]);
        putchar('\n');
    }
}

static const char *USAGE =
    "用法: mlfq [-f 输入文件] [-q 队列级数] [-t 基准时间片] [-a 老化周期] [-h]\n"
    "  多级反馈队列调度算法模拟。\n"
    "  -f <文件>      从文件读入进程，每行格式：名称 到达时间 服务时间 [I/O请求时间 I/O持续时间]\n"
    "  -q <级数>      就绪队列级数（1-%d，默认 %d），第 i 级时间片 = 基准时间片 × 2^(i-1)\n"
    "  -t <时间片>    第 1 级队列的基准时间片（默认 %d）\n"
    "  -a <周期>      老化周期，每隔该时间把所有就绪进程提升到第 1 级队列（默认 0，即关闭）\n"
    "  -h             显示本帮助\n";

int main(int argc, char **argv)
{
    Sim  s;
    Proc p[MAX_PROC];
    const char *file;
    int  n;

    os_lab_init_console();

    if (has_opt(argc, argv, "-h") || has_opt(argc, argv, "--help")) {
        printf(USAGE, MAX_LEVEL, DEFAULT_LEVELS, DEFAULT_SLICE);
        return 0;
    }
    warn_unknown_opts(argc, argv, "-f,-q,-t,-a");

    memset(&s, 0, sizeof s);
    s.levels     = opt_int(argc, argv, "-q", DEFAULT_LEVELS);
    s.base_slice = opt_int(argc, argv, "-t", DEFAULT_SLICE);
    s.aging      = opt_int(argc, argv, "-a", DEFAULT_AGING);

    if (s.levels < 1 || s.levels > MAX_LEVEL) {
        fprintf(stderr, "错误：队列级数必须在 1-%d 之间。\n", MAX_LEVEL);
        return 1;
    }
    if (s.base_slice < 1) {
        fprintf(stderr, "错误：基准时间片必须为正整数。\n");
        return 1;
    }
    if (s.aging < 0) {
        fprintf(stderr, "错误：老化周期不能为负。\n");
        return 1;
    }

    file = opt_value(argc, argv, "-f");
    n = file ? proc_load_file(file, p, MAX_PROC) : proc_prompt(p, MAX_PROC, 1);
    if (n <= 0) return 1;

    title_line("多级反馈队列（MLFQ）调度算法");
    printf("调度规则：新进程进入第 1 级队列；只有前面各级队列均为空时才调度第 i 级队列；\n"
           "          第 i 级队列时间片 = %d × 2^(i-1)；时间片用完未完成则降级；\n"
           "          更低优先级队列有进程到达时不抢占，更高优先级队列有进程到达时立即抢占；\n"
           "          I/O 完成后返回第 1 级队列；老化周期 = %s。\n",
           s.base_slice, s.aging > 0 ? "启用" : "关闭");
    if (s.aging > 0) printf("          老化周期为 %d 个时间单位。\n", s.aging);

    proc_print_input(p, n);

    proc_sort_by_arrival(p, n);
    proc_reset_stats(p, n);

    s.p = p;
    s.n = n;
    s.cur = -1;                               /* -1 表示当前没有进程占用处理机 */
    iq_init(&s.blocked);
    for (int l = 0; l < s.levels; l++) iq_init(&s.q[l]);
    for (int i = 0; i < n; i++) s.rem[i] = p[i].burst;

    simulate(&s);

    gantt_end();
    print_segments(&s);
    print_events();
    proc_print_stats(p, n, "各进程调度结果");
    print_queue_stats(&s);
    print_per_level_usage(&s);

    title_line("结果分析提示");
    printf("· 短作业（含交互型作业）在第 1 级队列中很快就能运行完，响应时间短；\n");
    printf("· 长作业被逐级下放，在低级队列中按较大时间片轮转，兼顾了系统吞吐量；\n");
    printf("· 若长作业长期得不到处理机，可启用老化策略（-a 参数）避免饥饿现象；\n");
    printf("· 队列级数、各级时间片的取值会影响系统性能，需根据作业类型分布折中选取。\n");
    return 0;
}
