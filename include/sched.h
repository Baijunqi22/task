/* ============================================================================
 *  sched.h —— 处理机调度与死锁实验 · 公共声明
 *
 *  本实验包含四个模拟程序：
 *    fcfs    先来先服务调度算法（非抢占）
 *    rr      时间片轮转调度算法（可选 I/O 阻塞）
 *    mlfq    多级反馈队列调度算法（抢占 + 降级 + 反馈 + 可选老化）
 *    banker  银行家算法（死锁避免）
 *
 *  编译环境：gcc (C11)，源码与运行输出均为 UTF-8。
 * ==========================================================================*/
#ifndef OS_LAB_SCHED_H
#define OS_LAB_SCHED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROC   64      /* 最多进程数 */
#define MAX_LEVEL  8       /* 多级反馈队列的最大级数 */
#define NAME_LEN   16      /* 进程名最大长度（含结束符） */
#define GANTT_CAP  8192    /* 甘特图缓存长度 */

/* ---------------------------------------------------------------- 进程控制块 */
typedef struct {
    int    pid;                 /* 进程编号（输入顺序，从 1 开始） */
    char   name[NAME_LEN];      /* 进程名 */
    int    arrive;              /* 到达时间 */
    int    burst;               /* 需要占用处理机的总时间（CPU 突发总长度） */
    int    io_after;            /* 已占用 CPU 多少时间后请求 I/O，0 表示无 I/O */
    int    io_time;             /* I/O 持续时间 */
    /* ---- 模拟结果 ---- */
    int    first_start;         /* 首次获得处理机的时间，-1 表示尚未运行 */
    int    finish;              /* 完成时间 */
    int    turnaround;          /* 周转时间 = 完成时间 - 到达时间 */
    double w_turnaround;        /* 带权周转时间 = 周转时间 / 服务时间 */
    int    wait;                /* 等待时间（处于就绪队列中的时间） */
} Proc;

/* ---------------------------------------------------- 控制台与 UTF-8 文本宽度 */
void os_lab_init_console(void);     /* Windows 下把控制台切到 UTF-8，避免中文乱码 */
int  text_width(const char *s);     /* 按显示宽度计算字符串长度（中文按 2 列） */
void cell(const char *s, int w);    /* 左对齐输出一列，按显示宽度补齐到 w 列 */
void cellf(int w, const char *fmt, ...);   /* cell 的格式化版本 */
void title_line(const char *title);        /* 打印 “----- 标题 -----” 分节行 */
void pause_enter(void);                    /* 等待用户按回车 */

/* -------------------------------------------------------------- 命令行参数解析 */
int         has_opt(int argc, char **argv, const char *name);
const char *opt_value(int argc, char **argv, const char *name);
int         opt_int(int argc, char **argv, const char *name, int def);
void        warn_unknown_opts(int argc, char **argv, const char *known);

/* ------------------------------------------------------------------ 进程输入 */
int  proc_load_file(const char *path, Proc *p, int max);   /* 从文件读入，返回个数，出错返回 -1 */
int  proc_prompt(Proc *p, int max, int ask_io);            /* 交互式输入 */
void proc_sort_by_arrival(Proc *p, int n);                 /* 按到达时间稳定排序 */
void proc_reset_stats(Proc *p, int n);                     /* 清零统计字段 */
int  has_io(const Proc *p, int n);                         /* 是否有进程需要 I/O */
void proc_print_input(const Proc *p, int n);               /* 打印输入数据表 */
void proc_print_stats(const Proc *p, int n, const char *title);  /* 打印调度结果统计表 */

/* -------------------------------------------------------------------- 甘特图 */
void gantt_begin(void);
void gantt_seg(const char *name, int start, int end);      /* name 为 NULL 表示处理机空闲 */
void gantt_end(void);

/* ---------------------------------------------------------------- 简易整数队列 */
#define IQ_CAP 2048
typedef struct {
    int a[IQ_CAP];
    int head, tail, cnt;
} IntQueue;

void iq_init(IntQueue *q);
int  iq_empty(const IntQueue *q);
int  iq_count(const IntQueue *q);
void iq_push(IntQueue *q, int v);
int  iq_pop(IntQueue *q);
int  iq_remove(IntQueue *q, int v);   /* 删除指定元素，成功返回 1 */

/* -------------------------------------------------------------------- 其他工具 */
int read_line(char *buf, int cap);              /* 读一行并去掉换行符，EOF 返回 0 */
int prompt_int(const char *prompt, int def);    /* 读入一个整数，输入非法时返回 def */

#endif /* OS_LAB_SCHED_H */
