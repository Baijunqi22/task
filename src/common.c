/* ============================================================================
 *  common.c —— 处理机调度与死锁实验 · 公共工具实现
 * ==========================================================================*/
#include "sched.h"
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <windows.h>
#endif

/* ====================================================== 控制台与字符串显示宽度 */

void os_lab_init_console(void)
{
#ifdef _WIN32
    /* 源程序与输出均为 UTF-8，把控制台代码页切到 65001 才能正确显示中文 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

/* 解码一个 UTF-8 字符，返回其占用的字节数 */
static int utf8_decode(const unsigned char *s, unsigned *cp)
{
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *cp = ((unsigned)(s[0] & 0x1F) << 6) | (unsigned)(s[1] & 0x3F);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((unsigned)(s[0] & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6) |
              (unsigned)(s[2] & 0x3F);
        return 3;
    }
    if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        *cp = ((unsigned)(s[0] & 0x07) << 18) | ((unsigned)(s[1] & 0x3F) << 12) |
              ((unsigned)(s[2] & 0x3F) << 6) | (unsigned)(s[3] & 0x3F);
        return 4;
    }
    *cp = s[0];
    return 1;
}

/* 判断某个码位在终端里是否占两列（中文、日文、韩文等宽字符） */
static int cp_is_wide(unsigned cp)
{
    return (cp >= 0x1100  && cp <= 0x115F)  || (cp >= 0x2E80  && cp <= 0x303E) ||
           (cp >= 0x3041  && cp <= 0x33FF)  || (cp >= 0x3400  && cp <= 0x4DBF) ||
           (cp >= 0x4E00  && cp <= 0x9FFF)  || (cp >= 0xA000  && cp <= 0xA4CF) ||
           (cp >= 0xAC00  && cp <= 0xD7A3)  || (cp >= 0xF900  && cp <= 0xFAFF) ||
           (cp >= 0xFE30  && cp <= 0xFE6F)  || (cp >= 0xFF00  && cp <= 0xFF60) ||
           (cp >= 0xFFE0  && cp <= 0xFFE6)  || (cp >= 0x20000 && cp <= 0x3FFFD);
}

int text_width(const char *s)
{
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;

    while (*p) {
        unsigned cp = 0;
        int len = utf8_decode(p, &cp);
        w += cp_is_wide(cp) ? 2 : 1;
        p += len;
    }
    return w;
}

void cell(const char *s, int w)
{
    int sw = text_width(s);

    fputs(s, stdout);
    while (sw++ < w) putchar(' ');
    putchar(' ');                 /* 列与列之间留一个空格 */
}

void cellf(int w, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    cell(buf, w);
}

void title_line(const char *title)
{
    printf("\n----- %s -----\n", title);
}

void pause_enter(void)
{
    char buf[32];

    printf("\n按回车键继续...");
    fflush(stdout);
    (void)!fgets(buf, sizeof buf, stdin);
}

/* ============================================================ 命令行参数解析 */

int has_opt(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], name) == 0) return 1;
    return 0;
}

const char *opt_value(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return NULL;
}

int opt_int(int argc, char **argv, const char *name, int def)
{
    const char *v = opt_value(argc, argv, name);

    if (!v) return def;
    return (int)strtol(v, NULL, 10);
}

/* known 是逗号分隔的选项名列表，例如 "-f,-t"；除 -h/-b 外所有选项都带一个取值 */
void warn_unknown_opts(int argc, char **argv, const char *known)
{
    for (int i = 1; i < argc; i++) {
        const char *tok = argv[i];

        if (strcmp(tok, "-h") == 0 || strcmp(tok, "--help") == 0 || strcmp(tok, "-b") == 0)
            continue;
        if (tok[0] != '-') continue;      /* 选项取值，跳过 */

        int found = 0;
        for (const char *p = known; p && *p; ) {
            const char *comma = strchr(p, ',');
            size_t len = comma ? (size_t)(comma - p) : strlen(p);

            if (strncmp(tok, p, len) == 0 && tok[len] == '\0') { found = 1; break; }
            p = comma ? comma + 1 : NULL;
        }
        if (found) {
            i++;                          /* 跳过该选项的取值 */
        } else {
            fprintf(stderr, "提示：忽略无法识别的参数 '%s'（用 -h 查看用法）\n", tok);
        }
    }
}

/* ============================================================== 文本解析辅助 */

static char *skip_blank(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* 去掉行尾的换行符与回车符 */
static void strip_eol(char *s)
{
    size_t len = strlen(s);

    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' ||
                       s[len - 1] == '\t'))
        s[--len] = '\0';
}

static int split_tokens(char *s, char *tok[], int maxtok)
{
    int n = 0;
    char *p = s;

    while (*p && n < maxtok) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !*s) return -1;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

/* 一行进程描述：名称 到达时间 服务时间 [I/O请求时间 I/O持续时间] */
static int parse_proc_line(char *line, Proc *p, int idx)
{
    char *tok[8];
    int nt = split_tokens(line, tok, 8);
    int arrive, burst, io_after = 0, io_time = 0;

    if (nt != 3 && nt != 5) return -1;
    if (strlen(tok[0]) >= NAME_LEN) return -1;
    if (parse_int(tok[1], &arrive) != 0 || parse_int(tok[2], &burst) != 0) return -1;
    if (nt == 5 && (parse_int(tok[3], &io_after) != 0 || parse_int(tok[4], &io_time) != 0))
        return -1;
    if (arrive < 0 || burst <= 0 || io_after < 0 || io_time < 0) return -1;
    if (io_after > burst) return -1;
    if (io_after > 0 && io_time <= 0) return -1;

    memset(p, 0, sizeof *p);
    snprintf(p->name, NAME_LEN, "%s", tok[0]);
    p->pid       = idx + 1;
    p->arrive    = arrive;
    p->burst     = burst;
    p->io_after  = io_after;
    p->io_time   = io_time;
    return 0;
}

/* ==================================================================== 进程输入 */

int proc_load_file(const char *path, Proc *p, int max)
{
    FILE *fp = fopen(path, "r");
    char line[512];
    int cnt = 0, first_data = 1, lineno = 0;

    if (!fp) {
        fprintf(stderr, "错误：无法打开输入文件 '%s'\n", path);
        return -1;
    }

    while (fgets(line, sizeof line, fp)) {
        char *hash, *s;
        char probe[512];
        char *tok[8];
        int nt;

        lineno++;
        hash = strchr(line, '#');
        if (hash) *hash = '\0';                 /* 支持 # 注释 */
        strip_eol(line);
        s = skip_blank(line);
        if (!*s) continue;

        /* 用副本试探字段个数，避免破坏原始行内容（错误提示需要原样输出） */
        snprintf(probe, sizeof probe, "%s", s);
        nt = split_tokens(probe, tok, 8);
        /* 允许文件首行为单独的进程个数，只是起提示作用 */
        if (first_data && nt == 1) {
            int dummy;
            if (parse_int(tok[0], &dummy) == 0) { first_data = 0; continue; }
        }
        if (cnt >= max) {
            fprintf(stderr, "错误：进程个数超过上限 %d\n", max);
            fclose(fp);
            return -1;
        }
        {
            char copy[512];

            snprintf(copy, sizeof copy, "%s", s);   /* 解析函数会就地分词，故传入副本 */
            if (parse_proc_line(copy, &p[cnt], cnt) != 0) {
                fprintf(stderr,
                        "错误：%s 第 %d 行格式不正确 -> \"%s\"\n"
                        "      正确格式：名称 到达时间 服务时间 [I/O请求时间 I/O持续时间]\n",
                        path, lineno, s);
                fclose(fp);
                return -1;
            }
        }
        cnt++;
        first_data = 0;
    }
    fclose(fp);

    if (cnt == 0) {
        fprintf(stderr, "错误：文件 '%s' 中没有有效的进程描述\n", path);
        return -1;
    }
    return cnt;
}

int proc_prompt(Proc *p, int max, int ask_io)
{
    char buf[512];
    int n;

    printf("\n请输入进程个数 (1-%d): ", max);
    fflush(stdout);
    if (!read_line(buf, sizeof buf)) return -1;
    n = (int)strtol(buf, NULL, 10);
    if (n < 1 || n > max) {
        printf("进程个数不合法，退出。\n");
        return -1;
    }

    if (ask_io)
        printf("请依次输入每个进程：名称 到达时间 服务时间 [I/O请求时间 I/O持续时间]\n"
               "（不需要 I/O 的进程只输入前 3 项即可）\n");
    else
        printf("请依次输入每个进程：名称 到达时间 服务时间\n");

    for (int i = 0; i < n; i++) {
        for (;;) {
            printf("  进程 %d: ", i + 1);
            fflush(stdout);
            if (!read_line(buf, sizeof buf)) return -1;
            if (parse_proc_line(buf, &p[i], i) == 0) break;
            printf("  输入格式错误（到达时间≥0，服务时间>0），请重新输入。\n");
        }
    }
    return n;
}

void proc_sort_by_arrival(Proc *p, int n)
{
    for (int i = 1; i < n; i++) {          /* 稳定插入排序：到达时间相同则保持输入顺序 */
        Proc key = p[i];
        int j = i - 1;

        while (j >= 0 && (p[j].arrive > key.arrive ||
                          (p[j].arrive == key.arrive && p[j].pid > key.pid))) {
            p[j + 1] = p[j];
            j--;
        }
        p[j + 1] = key;
    }
}

void proc_reset_stats(Proc *p, int n)
{
    for (int i = 0; i < n; i++) {
        p[i].first_start  = -1;
        p[i].finish       = -1;
        p[i].turnaround   = 0;
        p[i].w_turnaround = 0.0;
        p[i].wait         = 0;
    }
}

int has_io(const Proc *p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i].io_after > 0) return 1;
    return 0;
}

void proc_print_input(const Proc *p, int n)
{
    int with_io = has_io(p, n);

    title_line("输入的进程信息");
    cell("序号", 10);
    cell("进程名", 12);
    cell("到达时间", 12);
    cell("服务时间", 12);
    if (with_io) {
        cell("I/O请求时刻", 14);
        cell("I/O持续时间", 14);
    }
    putchar('\n');

    for (int i = 0; i < n; i++) {
        cellf(10, "%d", i + 1);
        cell(p[i].name, 12);
        cellf(12, "%d", p[i].arrive);
        cellf(12, "%d", p[i].burst);
        if (with_io) {
            cellf(14, "%d", p[i].io_after);
            cellf(14, "%d", p[i].io_time);
        }
        putchar('\n');
    }
}

void proc_print_stats(const Proc *p, int n, const char *title)
{
    double sum_turnaround = 0.0, sum_weighted = 0.0, sum_wait = 0.0;

    if (title) title_line(title);

    cell("进程名", 12);
    cell("到达时间", 12);
    cell("服务时间", 12);
    cell("开始时间", 12);
    cell("完成时间", 12);
    cell("周转时间", 12);
    cell("带权周转时间", 16);
    cell("等待时间", 12);
    cell("响应时间", 12);
    putchar('\n');

    for (int i = 0; i < n; i++) {
        cell(p[i].name, 12);
        cellf(12, "%d", p[i].arrive);
        cellf(12, "%d", p[i].burst);
        cellf(12, "%d", p[i].first_start);
        cellf(12, "%d", p[i].finish);
        cellf(12, "%d", p[i].turnaround);
        cellf(16, "%.2f", p[i].w_turnaround);
        cellf(12, "%d", p[i].wait);
        cellf(12, "%d", p[i].first_start >= 0 ? p[i].first_start - p[i].arrive : -1);
        putchar('\n');

        sum_turnaround += p[i].turnaround;
        sum_weighted   += p[i].w_turnaround;
        sum_wait       += p[i].wait;
    }
    printf("\n平均周转时间 = %.2f，平均带权周转时间 = %.2f，平均等待时间 = %.2f\n",
           sum_turnaround / n, sum_weighted / n, sum_wait / n);
}

/* ====================================================================== 甘特图 */

static char gantt_buf[GANTT_CAP];

void gantt_begin(void)
{
    gantt_buf[0] = '\0';
}

void gantt_seg(const char *name, int start, int end)
{
    char tmp[128];

    if (end <= start) return;
    snprintf(tmp, sizeof tmp, "| %s:%d-%d ", name ? name : "空闲", start, end);
    if (strlen(gantt_buf) + strlen(tmp) < GANTT_CAP - 2) strcat(gantt_buf, tmp);
}

void gantt_end(void)
{
    printf("\n甘特图：%s|\n", gantt_buf);
}

/* ================================================================ 简易整数队列 */

void iq_init(IntQueue *q)
{
    q->head = q->tail = q->cnt = 0;
}

int iq_empty(const IntQueue *q)
{
    return q->cnt == 0;
}

int iq_count(const IntQueue *q)
{
    return q->cnt;
}

void iq_push(IntQueue *q, int v)
{
    if (q->cnt >= IQ_CAP) return;          /* 超出容量时丢弃（正常情况下不会发生） */
    q->a[q->tail] = v;
    q->tail = (q->tail + 1) % IQ_CAP;
    q->cnt++;
}

int iq_pop(IntQueue *q)
{
    int v;

    if (q->cnt == 0) return -1;
    v = q->a[q->head];
    q->head = (q->head + 1) % IQ_CAP;
    q->cnt--;
    return v;
}

int iq_remove(IntQueue *q, int v)
{
    int found = 0, c = q->cnt;

    for (int i = 0; i < c; i++) {
        int x = iq_pop(q);
        if (x == v && !found) {
            found = 1;
            continue;
        }
        iq_push(q, x);
    }
    return found;
}

/* ==================================================================== 其他工具 */

int read_line(char *buf, int cap)
{
    size_t len;

    if (!fgets(buf, cap, stdin)) return 0;
    strip_eol(buf);
    len = strlen(buf);
    while (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return 1;
}

int prompt_int(const char *prompt, int def)
{
    char buf[64];

    printf("%s", prompt);
    fflush(stdout);
    if (!read_line(buf, sizeof buf)) return def;
    if (!buf[0]) return def;
    return (int)strtol(buf, NULL, 10);
}
