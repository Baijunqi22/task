/* ============================================================================
 *  banker.c —— 银行家算法模拟（死锁避免）
 *
 *  数据结构（m 类资源、n 个进程）：
 *    Available[j]      系统中第 j 类资源的当前可用量
 *    Max[i][j]         进程 i 对第 j 类资源的最大需求
 *    Allocation[i][j]  进程 i 已获得第 j 类资源的数量
 *    Need[i][j]        = Max[i][j] - Allocation[i][j]，进程尚需的资源量
 *
 *  安全性算法（判断系统是否处于安全状态）：
 *    令 Work = Available，Finish[i] = false；
 *    反复寻找满足 Finish[i]==false 且 Need[i] ≤ Work 的进程 i，
 *    找到后令 Work = Work + Allocation[i]、Finish[i] = true；
 *    若所有进程的 Finish 都为 true，则系统安全，且得到的进程顺序即为安全序列。
 *
 *  资源请求算法（进程 i 提出请求 Request_i）：
 *    ① 若 Request_i > Need_i，说明请求超过其最大需求，出错；
 *    ② 若 Request_i > Available，说明资源不足，请求不能被立即满足，进程等待；
 *    ③ 否则试探性地把资源分配给进程 i，并执行安全性算法：
 *        · 若系统仍处于安全状态，则正式分配；
 *        · 否则撤销本次试探性分配，让进程等待（避免系统进入不安全状态）。
 *
 *  用法: banker [-f 输入文件] [-b] [-h]
 *    -f <文件>  从文件读入初始状态（详见 README.md）
 *    -b         批处理模式：依次执行文件 REQUEST 段中的请求后退出
 * ==========================================================================*/
#include "sched.h"
#include <ctype.h>
#include <stdarg.h>

#define MAX_RES  16
#define MAX_REQ  128
#define MAXLINE  256
#define MAXLINES 512

typedef struct {
    int  m, n;                              /* 资源种类数、进程数 */
    char name[MAX_PROC][NAME_LEN];
    int  avail[MAX_RES];
    int  maxm[MAX_PROC][MAX_RES];
    int  alloc[MAX_PROC][MAX_RES];
    int  need[MAX_PROC][MAX_RES];
    int  total[MAX_RES];                    /* 各类资源的总量 */
    int  reqp[MAX_REQ];                     /* 请求序列：进程下标 */
    int  reqv[MAX_REQ][MAX_RES];            /* 请求序列：请求向量 */
    int  nreq;
} Bank;

static char lines[MAXLINES][MAXLINE];       /* 文件行缓存 */

/* ============================== 基本小工具 =============================== */

static void fmt_vec(char *buf, size_t cap, const int v[], int m)
{
    size_t used = 0;

    buf[0] = '\0';
    for (int j = 0; j < m; j++) {
        char t[24];
        size_t len;

        snprintf(t, sizeof t, "%s%d", j ? " " : "", v[j]);
        len = strlen(t);
        if (used + len + 1 >= cap) break;
        strcat(buf, t);
        used += len;
    }
}

/* 安全地把进程名拷贝到固定长度的缓冲区 */
static void copy_name(char *dst, const char *src)
{
    size_t len = strlen(src);

    if (len >= NAME_LEN) len = NAME_LEN - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void pv(const int v[], int m)            /* 打印一个资源向量：(3 3 2) */
{
    putchar('(');
    for (int j = 0; j < m; j++) printf("%s%d", j ? " " : "", v[j]);
    putchar(')');
}

static int le_vec(const int a[], const int b[], int m)   /* a ≤ b ? */
{
    for (int j = 0; j < m; j++)
        if (a[j] > b[j]) return 0;
    return 1;
}

static void calc_need(Bank *b)
{
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->need[i][j] = b->maxm[i][j] - b->alloc[i][j];
}

static void calc_total(Bank *b)
{
    for (int j = 0; j < b->m; j++) {
        int s = b->avail[j];
        for (int i = 0; i < b->n; i++) s += b->alloc[i][j];
        b->total[j] = s;
    }
}

/* ============================== 输出系统状态 ============================= */

static void print_state(const Bank *b)
{
    char v[128];

    title_line("当前系统状态");
    printf("资源种类数 m = %d，进程数 n = %d\n", b->m, b->n);

    printf("资源名           ：");
    for (int j = 0; j < b->m; j++) printf("R%-4d", j + 1);
    putchar('\n');
    printf("资源总量 Total   ：");
    for (int j = 0; j < b->m; j++) printf("%-5d", b->total[j]);
    putchar('\n');
    printf("当前可用 Available：");
    for (int j = 0; j < b->m; j++) printf("%-5d", b->avail[j]);
    putchar('\n');

    putchar('\n');
    cell("进程名", 10);
    cell("最大需求 Max", 20);
    cell("已分配 Allocation", 22);
    cell("尚需 Need", 20);
    putchar('\n');

    for (int i = 0; i < b->n; i++) {
        cell(b->name[i], 10);
        fmt_vec(v, sizeof v, b->maxm[i], b->m);
        cell(v, 20);
        fmt_vec(v, sizeof v, b->alloc[i], b->m);
        cell(v, 22);
        fmt_vec(v, sizeof v, b->need[i], b->m);
        cell(v, 20);
        putchar('\n');
    }
}

/* ============================== 安全性算法 =============================== */

/* 安全性算法：返回 1 表示系统安全，并把安全序列写入 seq */
static int is_safe(const Bank *b, int seq[MAX_PROC], int verbose)
{
    int work[MAX_RES], finish[MAX_PROC];
    int cnt = 0, step = 0;

    for (int i = 0; i < b->n; i++) finish[i] = 0;
    for (int j = 0; j < b->m; j++) work[j] = b->avail[j];

    if (verbose) {
        printf("\n安全性算法执行过程（Work 初值 = Available）：\n");
        cell("步骤", 8);
        cell("进程", 10);
        cell("Work 可用资源", 20);
        cell("Need 尚需资源", 20);
        cell("Work+Allocation", 22);
        cell("Finish", 10);
        putchar('\n');
    }

    while (cnt < b->n) {
        int found = -1;
        char w1[128], nd[128], w2[128];

        for (int i = 0; i < b->n; i++) {
            if (!finish[i] && le_vec(b->need[i], work, b->m)) { found = i; break; }
        }
        if (found < 0) break;                  /* 找不到可满足的进程 */

        fmt_vec(w1, sizeof w1, work, b->m);
        fmt_vec(nd, sizeof nd, b->need[found], b->m);
        for (int j = 0; j < b->m; j++) work[j] += b->alloc[found][j];
        fmt_vec(w2, sizeof w2, work, b->m);

        finish[found] = 1;
        seq[cnt++] = found;

        if (verbose) {
            cellf(8, "%d", ++step);
            cell(b->name[found], 10);
            cell(w1, 20);
            cell(nd, 20);
            cell(w2, 22);
            cell("true", 10);
            putchar('\n');
        }
    }

    if (verbose) {
        if (cnt == b->n) {
            printf("\n结论：存在安全序列，系统处于安全状态。安全序列为 ");
            for (int k = 0; k < cnt; k++) printf("%s%s", k ? " -> " : "", b->name[seq[k]]);
            printf("\n");
        } else {
            printf("\n结论：找不到安全序列，系统处于不安全状态。未能完成的进程：");
            for (int i = 0; i < b->n; i++)
                if (!finish[i]) printf("%s ", b->name[i]);
            printf("\n");
        }
    }
    return cnt == b->n;
}

/* ============================== 资源请求算法 ============================= */

/* 处理进程 pi 的资源请求 r：返回 1 表示分配成功，0 表示被拒绝（不分配） */
static int grant_request(Bank *b, int pi, const int r[], int verbose)
{
    char v1[128], v2[128], v3[128];
    int  seq[MAX_PROC], ok;

    printf("\n>>> 进程 %s 提出资源请求 Request = ", b->name[pi]);
    pv(r, b->m);
    printf("\n");

    /* ① Request ≤ Need ? */
    if (!le_vec(r, b->need[pi], b->m)) {
        printf("    检查 1：Request ≤ Need ？ 不满足。\n"
               "    该请求超过了进程 %s 自己声明的最大需求，属于非法请求，拒绝分配。\n",
               b->name[pi]);
        return 0;
    }
    printf("    检查 1：Request ≤ Need ？ 满足。\n");

    /* ② Request ≤ Available ? */
    if (!le_vec(r, b->avail, b->m)) {
        printf("    检查 2：Request ≤ Available ？ 不满足。\n"
               "    当前可用资源不足，资源请求不能被立即满足，进程 %s 必须等待。\n",
               b->name[pi]);
        return 0;
    }
    printf("    检查 2：Request ≤ Available ？ 满足，进行试探性分配。\n");

    /* ③ 试探性分配并检查安全性 */
    for (int j = 0; j < b->m; j++) {
        b->avail[j]      -= r[j];
        b->alloc[pi][j]  += r[j];
        b->need[pi][j]   -= r[j];
    }
    fmt_vec(v1, sizeof v1, b->avail, b->m);
    fmt_vec(v2, sizeof v2, b->alloc[pi], b->m);
    fmt_vec(v3, sizeof v3, b->need[pi], b->m);
    printf("    试探性分配后：Available = (%s)，%s 的 Allocation = (%s)，Need = (%s)\n",
           v1, b->name[pi], v2, v3);

    ok = is_safe(b, seq, verbose);

    if (ok) {
        printf("    ==> 分配后系统仍处于安全状态，本次分配成功，资源正式分配给 %s。\n",
               b->name[pi]);
    } else {
        for (int j = 0; j < b->m; j++) {          /* 撤销试探性分配 */
            b->avail[j]     += r[j];
            b->alloc[pi][j] -= r[j];
            b->need[pi][j]  += r[j];
        }
        printf("    ==> 该分配会使系统进入不安全状态，撤销本次试探性分配，%s 必须等待。\n",
               b->name[pi]);
    }
    return ok;
}

/* ============================ 输入：教材示例数据 ========================== */

static void load_example(Bank *b)
{
    static const int av[3]  = {3, 3, 2};
    static const int mx[5][3] = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
    static const int al[5][3] = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};

    memset(b, 0, sizeof *b);
    b->m = 3;
    b->n = 5;
    for (int i = 0; i < b->n; i++) snprintf(b->name[i], NAME_LEN, "P%d", i);
    for (int j = 0; j < b->m; j++) b->avail[j] = av[j];
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++) {
            b->maxm[i][j]  = mx[i][j];
            b->alloc[i][j] = al[i][j];
        }
    calc_need(b);
    calc_total(b);
}

/* ============================= 输入：手工输入 ============================= */

static int ask_vec(const char *prompt, int out[], int m)
{
    char buf[MAXLINE];

    for (;;) {
        int bad = 0;

        printf("%s", prompt);
        fflush(stdout);
        if (!read_line(buf, sizeof buf)) return -1;

        /* 逐个解析 m 个整数 */
        {
            char *p = buf;
            int   k = 0;

            while (*p && k < m) {
                char *end = NULL;
                long  x;

                while (*p == ' ' || *p == '\t' || *p == ',') p++;
                if (!*p) break;
                x = strtol(p, &end, 10);
                if (end == p) { k = -1; break; }
                out[k++] = (int)x;
                p = end;
            }
            if (k != m) {
                printf("  需要 %d 个整数，请重新输入。\n", m);
                continue;
            }
        }
        for (int j = 0; j < m; j++)
            if (out[j] < 0) bad = 1;
        if (bad) {
            printf("  资源数量不能为负数，请重新输入。\n");
            continue;
        }
        return 0;
    }
}

static int input_manual(Bank *b)
{
    memset(b, 0, sizeof *b);

    b->m = prompt_int("资源种类数 m (1-16): ", 3);
    b->n = prompt_int("进程数 n (1-64): ", 5);
    if (b->m < 1 || b->m > MAX_RES || b->n < 1 || b->n > MAX_PROC) {
        printf("输入的范围不合法。\n");
        return -1;
    }
    for (int i = 0; i < b->n; i++) snprintf(b->name[i], NAME_LEN, "P%d", i);

    if (ask_vec("请输入各类资源的当前可用量 Available: ", b->avail, b->m) != 0) return -1;

    for (int i = 0; i < b->n; i++) {
        char prompt[128];

        for (;;) {
            snprintf(prompt, sizeof prompt, "进程 %s 的最大需求 Max: ", b->name[i]);
            if (ask_vec(prompt, b->maxm[i], b->m) != 0) return -1;
            snprintf(prompt, sizeof prompt, "进程 %s 的已分配 Allocation: ", b->name[i]);
            if (ask_vec(prompt, b->alloc[i], b->m) != 0) return -1;

            if (!le_vec(b->alloc[i], b->maxm[i], b->m)) {
                printf("  已分配量不能超过最大需求，请重新输入进程 %s 的数据。\n", b->name[i]);
                continue;
            }
            break;
        }
    }
    calc_need(b);
    calc_total(b);
    return 0;
}

/* ============================== 输入：文件解析 =========================== */

enum { K_NONE = -1, K_M, K_N, K_AVAIL, K_ALLOC, K_MAX, K_REQ };

static int key_of(const char *line)
{
    char key[32];
    const char *sep = NULL;
    size_t len;

    for (const char *p = line; *p; p++)
        if (*p == ':' || *p == '=') { sep = p; break; }
    if (!sep) return K_NONE;

    len = (size_t)(sep - line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) len--;
    if (len == 0 || len >= sizeof key) return K_NONE;
    memcpy(key, line, len);
    key[len] = '\0';
    for (char *p = key; *p; p++) *p = (char)toupper((unsigned char)*p);

    if (strcmp(key, "M") == 0 || strcmp(key, "资源种类") == 0 ||
        strcmp(key, "资源种类数") == 0 || strcmp(key, "RESOURCES") == 0) return K_M;
    if (strcmp(key, "N") == 0 || strcmp(key, "进程数") == 0 ||
        strcmp(key, "PROCESSES") == 0) return K_N;
    if (strcmp(key, "AVAILABLE") == 0 || strcmp(key, "可用资源") == 0) return K_AVAIL;
    if (strcmp(key, "ALLOCATION") == 0 || strcmp(key, "已分配") == 0) return K_ALLOC;
    if (strcmp(key, "MAX") == 0 || strcmp(key, "最大需求") == 0) return K_MAX;
    if (strcmp(key, "REQUEST") == 0 || strcmp(key, "请求") == 0) return K_REQ;
    return K_NONE;
}

static const char *key_body(const char *line)
{
    const char *sep = NULL;

    for (const char *p = line; *p; p++)
        if (*p == ':' || *p == '=') { sep = p; break; }
    return sep ? sep + 1 : "";
}

static char *skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int split_words(char *s, char *tok[], int maxtok)
{
    int n = 0;
    char *p = s;

    while (*p && n < maxtok) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* 把 s 中的整数依次写入 out，返回个数 */
static int scan_ints(const char *s, int out[], int cap)
{
    const char *p = s;
    int n = 0;

    while (*p && n < cap) {
        char *end = NULL;
        long v;

        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        v = strtol(p, &end, 10);
        if (end == p) break;                   /* 遇到非数字，停止 */
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

/* 收集关键字行 start 之后的数据行下标（遇到下一个关键字行为止） */
static int collect_section(int nl, int start, int idx[], int cap)
{
    int cnt = 0;

    for (int k = start + 1; k < nl; k++) {
        if (key_of(lines[k]) != K_NONE) break;
        if (cnt < cap) idx[cnt] = k;
        cnt++;
    }
    return cnt;
}

/* 解析矩阵中的一行：m+1 个字段时首字段是进程名，m 个字段时只有数据 */
static int parse_matrix_row(char *row, int m, int out[], char name_out[], int *has_name)
{
    char *tok[MAX_RES + 4];
    int nt = split_words(row, tok, MAX_RES + 4);
    int base = 0;

    if (nt == m + 1) {
        *has_name = 1;
        copy_name(name_out, tok[0]);
        base = 1;
    } else if (nt == m) {
        *has_name = 0;
    } else {
        return -1;
    }
    for (int j = 0; j < m; j++) {
        char *end = NULL;
        long v = strtol(tok[base + j], &end, 10);

        if (end == tok[base + j] || *end != '\0' || v < 0) return -1;
        out[j] = (int)v;
    }
    return 0;
}

static int find_proc(const Bank *b, const char *tok)
{
    char *end = NULL;
    long v = strtol(tok, &end, 10);

    if (end != tok && *end == '\0') {               /* 纯数字：按编号 1..n 或 0..n-1 */
        if (v >= 1 && v <= b->n) return (int)v - 1;
        if (v >= 0 && v < b->n)  return (int)v;
        return -1;
    }
    for (int i = 0; i < b->n; i++)
        if (strcmp(b->name[i], tok) == 0) return i;
    return -1;
}

static int load_banker_file(const char *path, Bank *b)
{
    FILE *fp = fopen(path, "r");
    int  nl = 0, m = -1, n = -1;
    int  seen_alloc = 0, seen_max = 0;

    if (!fp) {
        fprintf(stderr, "错误：无法打开输入文件 '%s'\n", path);
        return -1;
    }
    memset(b, 0, sizeof *b);

    /* ---- 读入并预处理 ---- */
    while (fgets(lines[nl], MAXLINE, fp)) {
        char *hash, *s;
        size_t len;

        hash = strchr(lines[nl], '#');
        if (hash) *hash = '\0';
        len = strlen(lines[nl]);
        while (len > 0 && (lines[nl][len - 1] == '\n' || lines[nl][len - 1] == '\r' ||
                           lines[nl][len - 1] == ' '  || lines[nl][len - 1] == '\t'))
            lines[nl][--len] = '\0';
        s = skip_ws(lines[nl]);
        if (!*s) continue;
        if (s != lines[nl]) memmove(lines[nl], s, strlen(s) + 1);
        if (nl < MAXLINES - 1) nl++;
    }
    fclose(fp);

    /* ---- 第一遍：取 m、n ---- */
    for (int i = 0; i < nl; i++) {
        int k = key_of(lines[i]);
        int v;

        if (k == K_M && scan_ints(key_body(lines[i]), &v, 1) == 1) m = v;
        if (k == K_N && scan_ints(key_body(lines[i]), &v, 1) == 1) n = v;
    }
    if (m < 0) {
        fprintf(stderr, "错误：文件缺少资源种类数 M。\n");
        return -1;
    }
    b->m = m;                       /* 先确定 m、n，后面的数据段与请求段都要用到 */
    if (n > 0) b->n = n;

    /* ---- 第二遍：各数据段 ---- */
    for (int i = 0; i < nl; i++) {
        int k = key_of(lines[i]);
        int idx[MAXLINES], cnt;

        if (k == K_NONE) continue;
        cnt = collect_section(nl, i, idx, MAXLINES);

        if (k == K_AVAIL) {
            int tmp[MAX_RES * 4], nv;

            if (m < 0) { fprintf(stderr, "错误：文件缺少资源种类数 M。\n"); return -1; }
            nv = scan_ints(key_body(lines[i]), tmp, MAX_RES * 4);
            for (int a = 0; a < cnt && nv < m; a++)
                nv += scan_ints(lines[idx[a]], tmp + nv, MAX_RES * 4 - nv);
            if (nv < m) {
                fprintf(stderr, "错误：Available 段需要 %d 个整数，实际只有 %d 个。\n", m, nv);
                return -1;
            }
            for (int j = 0; j < m; j++) b->avail[j] = tmp[j];
        } else if (k == K_ALLOC || k == K_MAX) {
            int mat[MAXLINES][MAX_RES];
            char nm[MAX_PROC][NAME_LEN];
            int  has_name_any = 0;
            int  rows = 0;

            if (m < 0) { fprintf(stderr, "错误：文件缺少资源种类数 M。\n"); return -1; }
            for (int a = 0; a < cnt; a++) {
                char tmp[MAXLINE];
                int  row[MAX_RES], has_name = 0;
                char rname[NAME_LEN] = "";

                if (rows >= MAX_PROC) {
                    fprintf(stderr, "错误：进程数超过上限 %d。\n", MAX_PROC);
                    return -1;
                }
                snprintf(tmp, sizeof tmp, "%s", lines[idx[a]]);
                if (parse_matrix_row(tmp, m, row, rname, &has_name) != 0) {
                    fprintf(stderr, "错误：数据行格式不正确 -> \"%s\"\n", lines[idx[a]]);
                    return -1;
                }
                if (has_name) {
                    has_name_any = 1;
                    copy_name(nm[rows], rname);
                } else {
                    snprintf(nm[rows], NAME_LEN, "P%d", rows);
                }
                for (int j = 0; j < m; j++) mat[rows][j] = row[j];
                rows++;
            }
            if (rows == 0) {
                fprintf(stderr, "错误：%s 段没有数据。\n",
                        k == K_ALLOC ? "Allocation" : "Max");
                return -1;
            }
            if (n < 0) { n = rows; b->n = rows; }
            if (rows != n) {
                fprintf(stderr, "错误：%s 段有 %d 行，而进程数 n = %d。\n",
                        k == K_ALLOC ? "Allocation" : "Max", rows, n);
                return -1;
            }
            /* 记录进程名（以先出现的名字为准） */
            for (int a = 0; a < rows; a++) {
                if (k == K_ALLOC || b->name[a][0] == '\0')
                    copy_name(b->name[a], nm[a]);
            }
            (void)has_name_any;
            for (int a = 0; a < rows; a++)
                for (int j = 0; j < m; j++) {
                    if (k == K_ALLOC) b->alloc[a][j] = mat[a][j];
                    else              b->maxm[a][j]  = mat[a][j];
                }
            if (k == K_ALLOC) seen_alloc = 1; else seen_max = 1;
        } else if (k == K_REQ) {
            if (m < 0) { fprintf(stderr, "错误：文件缺少资源种类数 M。\n"); return -1; }
            for (int a = 0; a < cnt; a++) {
                char tmp[MAXLINE];
                char *tok[MAX_RES + 4];
                int  nt, pi;

                snprintf(tmp, sizeof tmp, "%s", lines[idx[a]]);
                nt = split_words(tmp, tok, MAX_RES + 4);
                if (nt != m + 1) {
                    fprintf(stderr, "错误：Request 段每行应为“进程名(或编号) + %d 个整数”-> \"%s\"\n",
                            m, lines[idx[a]]);
                    return -1;
                }
                pi = find_proc(b, tok[0]);
                if (pi < 0) {
                    fprintf(stderr, "错误：Request 段中找不到进程 '%s'。\n", tok[0]);
                    return -1;
                }
                if (b->nreq >= MAX_REQ) {
                    fprintf(stderr, "错误：请求个数超过上限 %d。\n", MAX_REQ);
                    return -1;
                }
                b->reqp[b->nreq] = pi;
                for (int j = 0; j < m; j++) {
                    char *end = NULL;
                    long v = strtol(tok[1 + j], &end, 10);

                    if (end == tok[1 + j] || *end != '\0' || v < 0) {
                        fprintf(stderr, "错误：请求向量中的元素非法 -> \"%s\"\n", tok[1 + j]);
                        return -1;
                    }
                    b->reqv[b->nreq][j] = (int)v;
                }
                b->nreq++;
            }
        }
    }

    if (m < 0 || n < 0) {
        fprintf(stderr, "错误：文件信息不足（需要 M、N、AVAILABLE 以及 MAX、ALLOCATION）。\n");
        return -1;
    }
    if (!seen_alloc || !seen_max) {
        fprintf(stderr, "错误：文件必须同时给出 ALLOCATION 与 MAX 两个数据段。\n");
        return -1;
    }
    b->m = m;
    b->n = n;

    calc_need(b);
    calc_total(b);

    for (int i = 0; i < b->n; i++) {
        if (!le_vec(b->alloc[i], b->maxm[i], b->m)) {
            fprintf(stderr, "错误：进程 %s 的已分配量超过了它的最大需求。\n", b->name[i]);
            return -1;
        }
    }
    return 0;
}

/* ================================== 主程序 =============================== */

static const char *USAGE =
    "用法: banker [-f 输入文件] [-b] [-h]\n"
    "  银行家算法（死锁避免）模拟。\n"
    "  -f <文件>  从文件读入初始状态，文件格式见 README.md：\n"
    "               M: 3 / N: 5 / AVAILABLE: 3 3 2 / ALLOCATION: ... / MAX: ... / REQUEST: ...\n"
    "  -b         批处理模式：依次执行文件 REQUEST 段中的请求，输出结果后退出\n"
    "  -h         显示本帮助\n"
    "不带 -f 时可以使用教材示例数据或手工输入。\n";

int main(int argc, char **argv)
{
    Bank b;
    const char *file;
    int  batch;
    char buf[64];

    os_lab_init_console();

    if (has_opt(argc, argv, "-h") || has_opt(argc, argv, "--help")) {
        fputs(USAGE, stdout);
        return 0;
    }
    warn_unknown_opts(argc, argv, "-f");
    batch = has_opt(argc, argv, "-b");
    file  = opt_value(argc, argv, "-f");

    title_line("银行家算法模拟（死锁避免）");

    if (file) {
        if (load_banker_file(file, &b) != 0) return 1;
        printf("已从文件 '%s' 读入初始状态。\n", file);
    } else {
        printf("请选择初始数据的输入方式：\n"
               "  1 - 手工输入\n"
               "  2 - 使用教材示例数据（5 个进程、3 类资源）\n");
        int ch = prompt_int("请选择 [1/2，直接回车表示 2]: ", 2);

        if (ch == 1) {
            if (input_manual(&b) != 0) return 1;
        } else {
            load_example(&b);
            printf("已载入教材示例数据。\n");
        }
    }

    print_state(&b);

    /* ------------------------------ 批处理模式 ----------------------------- */
    if (batch) {
        if (b.nreq == 0) {
            printf("\n注意：输入文件没有 REQUEST 段，仅做一次安全性检查。\n");
        }
        int seq[MAX_PROC];

        is_safe(&b, seq, 1);
        for (int k = 0; k < b.nreq; k++) {
            printf("\n================ 第 %d 个资源请求 =================\n", k + 1);
            grant_request(&b, b.reqp[k], b.reqv[k], 1);
            print_state(&b);
        }
        printf("\n================ 批处理结束，最终系统状态 ================\n");
        if (is_safe(&b, seq, 0))
            printf("最终状态安全性检查：系统处于安全状态。\n");
        else
            printf("最终状态安全性检查：系统处于不安全状态。\n");
        return 0;
    }

    /* ------------------------------- 交互菜单 ------------------------------ */
    for (;;) {
        int ch;

        printf("\n============== 银行家算法主菜单 ==============\n");
        printf("  1. 显示当前系统状态（Max / Allocation / Need / Available）\n");
        printf("  2. 进行安全性检查（输出安全性算法执行过程与安全序列）\n");
        printf("  3. 提出资源请求 Request（按银行家算法判断能否分配）\n");
        if (b.nreq > 0)
            printf("  4. 依次执行输入文件中的 %d 个资源请求\n", b.nreq);
        printf("  0. 退出\n请选择: ");
        fflush(stdout);
        if (!read_line(buf, sizeof buf)) break;
        ch = (int)strtol(buf, NULL, 10);

        if (ch == 0) break;
        if (ch == 1) {
            print_state(&b);
        } else if (ch == 2) {
            int seq[MAX_PROC];

            is_safe(&b, seq, 1);
        } else if (ch == 3) {
            char nm[NAME_LEN], line[MAXLINE];
            int  pi = -1, r[MAX_RES];

            printf("请输入提出请求的进程名（或编号 1-%d）: ", b.n);
            fflush(stdout);
            if (!read_line(nm, sizeof nm)) break;
            if (!nm[0]) { printf("输入为空，已取消。\n"); continue; }
            pi = find_proc(&b, nm);
            if (pi < 0) { printf("找不到进程 '%s'，已取消。\n", nm); continue; }

            printf("请输入 %s 的请求向量（%d 个整数，以空格分隔）: ", b.name[pi], b.m);
            fflush(stdout);
            if (!read_line(line, sizeof line)) break;
            if (scan_ints(line, r, b.m) != b.m) {
                printf("需要 %d 个整数，已取消。\n", b.m);
                continue;
            }
            grant_request(&b, pi, r, 1);
            print_state(&b);
        } else if (ch == 4 && b.nreq > 0) {
            for (int k = 0; k < b.nreq; k++) {
                printf("\n================ 第 %d 个资源请求 =================\n", k + 1);
                grant_request(&b, b.reqp[k], b.reqv[k], 1);
                print_state(&b);
            }
        } else {
            printf("无此选项，请重新选择。\n");
        }
    }
    printf("\n程序结束。\n");
    return 0;
}
