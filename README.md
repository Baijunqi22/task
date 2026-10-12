# 处理机调度与死锁 —— 实验说明与使用手册

本实验用 C 语言模拟实现了处理机调度与死锁避免中的四个经典算法，全部程序均为单文件、
纯标准 C（C11）实现，不依赖任何第三方库。

| 程序 | 对应实验要求 | 内容 |
| --- | --- | --- |
| `fcfs` | 要求(2) | 先来先服务（FCFS）调度算法，非抢占 |
| `rr` | 实验目的中提到的“时间片轮转” | 时间片轮转（RR）调度算法，支持进程请求 I/O |
| `mlfq` | 要求(3) | 多级反馈队列（MLFQ）调度算法：抢占、降级、反馈、可选老化 |
| `banker` | 要求(4) | 银行家算法：安全性检查与资源请求判定（死锁避免） |

## 一、目录结构

```
os-lab-scheduling-deadlock/
├── include/sched.h        公共数据结构（PCB）、函数声明
├── src/common.c           公共工具：输入解析、统计表、甘特图、整数队列
├── src/fcfs.c             先来先服务调度算法
├── src/rr.c               时间片轮转调度算法
├── src/mlfq.c             多级反馈队列调度算法
├── src/banker.c           银行家算法
├── test/                  测试数据（*.txt）
├── docs/实验报告.md        实验报告（含测试结果与分析）
├── docs/output/            run_tests.ps1 自动生成的运行结果（每个用例一个文件）
├── build.ps1              编译脚本（Windows + gcc）
├── run_tests.ps1          一键编译并运行全部测试用例
└── Makefile               使用 make 的编译方式（可选）
```

## 二、编译

环境要求：支持 C11 的 gcc（MinGW-w64 / MSYS2 均可）。

**方式一（推荐，Windows + PowerShell）**

```powershell
cd os-lab-scheduling-deadlock
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

可执行文件生成在 `bin\` 目录下。

**方式二（gcc 命令行）**

```powershell
gcc -std=c11 -Wall -Wextra -O2 -Iinclude -o bin\fcfs.exe   src\common.c src\fcfs.c
gcc -std=c11 -Wall -Wextra -O2 -Iinclude -o bin\rr.exe     src\common.c src\rr.c
gcc -std=c11 -Wall -Wextra -O2 -Iinclude -o bin\mlfq.exe   src\common.c src\mlfq.c
gcc -std=c11 -Wall -Wextra -O2 -Iinclude -o bin\banker.exe src\common.c src\banker.c
```

**方式三（已安装 make 时）**

```bash
make          # 或 make all
make clean
```

> 如果控制台中文显示为乱码，先执行 `chcp 65001` 把代码页切到 UTF-8；
> 程序内部也会在 Windows 下自动调用 `SetConsoleOutputCP(65001)`。

## 三、运行

### 1. 调度类程序（fcfs / rr / mlfq）

不带参数运行则从键盘输入；用 `-f` 指定数据文件；用 `-h` 查看帮助。

```powershell
.\bin\fcfs.exe -f test\fcfs_1.txt              # 先来先服务
.\bin\rr.exe   -f test\rr_1.txt -t 1           # 时间片轮转，时间片 = 1
.\bin\rr.exe   -f test\rr_2.txt -t 2           # 含 I/O 的时间片轮转
.\bin\mlfq.exe -f test\mlfq_1.txt              # 多级反馈队列（3 级、基准时间片 1）
.\bin\mlfq.exe -f test\mlfq_1.txt -q 4 -t 2    # 4 级队列、基准时间片 2
.\bin\mlfq.exe -f test\mlfq_3.txt -a 5         # 启用老化：每 5 个时间单位提升一次
.\bin\mlfq.exe -h                               # 查看全部参数
```

| 参数 | 适用程序 | 含义 |
| --- | --- | --- |
| `-f <文件>` | 全部 | 从文件读入进程，不指定则键盘输入 |
| `-t <时间片>` | rr / mlfq | rr：时间片长度（默认 2）；mlfq：第 1 级队列的基准时间片（默认 1） |
| `-q <级数>` | mlfq | 就绪队列级数（1~8，默认 3），第 i 级时间片 = 基准时间片 × 2^(i-1) |
| `-a <周期>` | mlfq | 老化周期，0 表示关闭（默认 0） |
| `-h` | 全部 | 显示帮助 |

### 2. 银行家算法（banker）

```powershell
.\bin\banker.exe -f test\banker_1.txt              # 载入数据后进入交互菜单
.\bin\banker.exe -f test\banker_1.txt -b           # 批处理：依次执行文件中全部请求后退出
.\bin\banker.exe                                   # 手工输入或使用教材示例数据
```

不带 `-f` 运行时，可以先选择“使用教材示例数据（5 个进程、3 类资源）”，再进入主菜单：

```
  1. 显示当前系统状态（Max / Allocation / Need / Available）
  2. 进行安全性检查（输出安全性算法执行过程与安全序列）
  3. 提出资源请求 Request（按银行家算法判断能否分配）
  4. 依次执行输入文件中的资源请求（仅当文件包含 REQUEST 段时出现）
  0. 退出
```

## 四、输入文件格式

### 1. 调度类程序（fcfs / rr / mlfq）

每行描述一个进程，字段之间用空格或制表符分隔；`#` 之后为注释；允许空行；
文件第一行也可以单独写一个进程个数（程序会忽略它）：

```
# 进程名  到达时间  服务时间  [I/O请求时刻  I/O持续时间]
P1 0 4
P2 2 3
P3 3 6
P4 5 8 2 3      # P4 在占用 CPU 2 个时间单位后请求 I/O，I/O 持续 3 个时间单位
```

说明：

* 到达时间 ≥ 0，服务时间 > 0，时间单位由用户自行约定（本实验按整数时间单位模拟）；
* I/O 字段可省略；填写时要求 0 < I/O请求时刻 ≤ 服务时间，I/O 持续时间 > 0；
* 不需要 I/O 的程序（FCFS）会忽略后两个字段。

### 2. 银行家算法（banker）

使用“关键字: 数据”的形式，`#` 之后为注释，支持 `:` 或 `=`：

```
M: 3                       # 资源种类数
N: 5                       # 进程数（可省略，由 ALLOCATION 段行数推断）
AVAILABLE: 3 3 2           # 各类资源的当前可用量
ALLOCATION:                # 已分配矩阵，每行：进程名 + m 个整数（进程名可省略）
  P0 0 1 0
  P1 2 0 0
MAX:                       # 最大需求矩阵，格式同 ALLOCATION
  P0 7 5 3
  P1 3 2 2
REQUEST:                   # 可选：资源请求序列，每行“进程名(或编号) + m 个整数”
  P1 1 0 2
```

数据段必须写在它自己的行下面（`AVAILABLE` 可以写成行内形式）；
`ALLOCATION`、`MAX`、`REQUEST` 段中同时给出进程名或只给数字都可以。

## 五、程序实现的算法规则

所有程序采用的时间单位与事件次序约定：

* 时间按整数单位推进（下文用“时刻”表示）；
* 同一时刻的事件处理次序为：**① I/O 完成唤醒 → ② 新进程到达入队 → ③ 老化提升 →
  ④ 抢占判断 → ⑤ 处理当前进程的“完成 / 请求 I/O / 时间片用完重新入队”**；
* rr 与 mlfq 中，同一时刻到达的新进程排在刚被剥夺 CPU 的进程之前。

### 1. FCFS（先来先服务）

按到达时间（相同则按输入顺序）建立就绪队列，队首进程一直运行到完毕，非抢占；
队首进程尚未到达时处理机空闲等待。评价指标：周转时间、带权周转时间、等待时间、响应时间。

### 2. RR（时间片轮转）

就绪队列 FIFO，队首进程运行一个时间片；时间片用完未完成则排到队尾；
进程请求 I/O 时让出处理机，I/O 完成后排到队尾。

### 3. MLFQ（多级反馈队列）

1. 第 1 级队列优先级最高，第 i 级队列的时间片为 `基准时间片 × 2^(i-1)`；
2. 新到达的进程一律进入第 1 级队列队尾；
3. 只有第 1 ~ i-1 级队列都为空时，才调度第 i 级队列中的进程，同级队列内部按时间片轮转；
4. 用完了第 i 级队列的时间片仍未完成的进程，降入第 i+1 级队列队尾；
   在最低一级队列中不再降级，重新排到本队列队尾继续轮转；
5. 更高优先级队列中有进程到达时立即抢占；被抢占的进程回到当前队列队尾，时间片重新计时；
6. 因 I/O 让出处理机的进程，I/O 完成后返回第 1 级队列（体现“反馈”）；
7. 老化策略（`-a` 开启）：每隔固定时间把所有就绪进程提升到第 1 级队列，避免长作业饥饿。

### 4. 银行家算法

* **安全性算法**：令 `Work = Available`，反复寻找 `Need[i] ≤ Work` 且未完成的进程 i，
  找到后令 `Work += Allocation[i]`（表示该进程能顺利执行完并归还资源），
  若所有进程都能完成则系统安全，得到的次序就是一个安全序列；
* **资源请求算法**：进程 i 请求 `Request` 时依次判断
  ① `Request ≤ Need[i]`（否则请求非法）、② `Request ≤ Available`（否则资源不足、进程等待）、
  ③ 试探性分配后系统是否仍然安全（不安全则撤销本次分配，令进程等待）。

## 六、测试数据一览

| 用例文件 | 运行命令 | 观察重点 |
| --- | --- | --- |
| `test/fcfs_1.txt` | `fcfs -f test\fcfs_1.txt` | 到达时间间隔造成的处理机空闲 |
| `test/fcfs_2.txt` | `fcfs -f test\fcfs_2.txt` | 同时到达时按输入顺序执行 |
| `test/rr_1.txt` | `rr -f test\rr_1.txt -t 1` | 时间片轮转的执行片段与事件时间线 |
| `test/rr_2.txt` | `rr -f test\rr_2.txt -t 2` | 进程请求 I/O 时的让出与唤醒 |
| `test/mlfq_1.txt` | `mlfq -f test\mlfq_1.txt` | 抢占、逐级降级、低级队列轮转 |
| `test/mlfq_2.txt` | `mlfq -f test\mlfq_2.txt` | I/O 完成后返回第 1 级队列（反馈） |
| `test/mlfq_3.txt` | `mlfq -f test\mlfq_3.txt -a 5` | 老化提升，避免长作业长期得不到处理机 |
| `test/banker_1.txt` | `banker -f test\banker_1.txt -b` | 教材例题：可以分配 / 可用资源不足 / 不安全而撤销 |
| `test/banker_2.txt` | `banker -f test\banker_2.txt -b` | 典型的不安全请求（试探分配后找不到安全序列） |
| `test/banker_3.txt` | `banker -f test\banker_3.txt -b` | 非法请求与资源不足两种拒绝情形 |
| `test/banker_4.txt` | `banker -f test\banker_4.txt -b` | 4 类资源，验证程序的通用性 |
| `test/compare.txt` | 见 `run_tests.ps1` | 同一组作业在三种调度算法下的对比 |

一键运行全部用例并把结果写入 `docs/output/`：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_tests.ps1
powershell -ExecutionPolicy Bypass -File .\run_tests.ps1 -NoBuild   # 不重新编译
```

## 七、常见问题

**1. 输出中文是乱码？**
在 Windows 传统控制台（cmd）中先执行 `chcp 65001`，或者用 Windows Terminal / VS Code 终端运行。
程序已自动把输出代码页设置为 UTF-8，若仍然乱码通常是终端字体不支持中文。

**2. 提示“找不到 gcc”？**
安装 MSYS2 或 MinGW-w64，并把 `gcc.exe` 所在目录加入 `PATH`，例如 `D:\msys64\ucrt64\bin`。

**3. 输入文件解析失败？**
检查是否为“名称 到达时间 服务时间”三个字段（或多加 I/O 的两个字段），
且文件为 UTF-8 或 GBK 编码均可（程序按字节读取，仅要求 ASCII 字符与数字）。

**4. 想改成实时演示或加入更多算法？**
`include/sched.h` 中的 `Proc` 结构体与 `src/common.c` 中的输入、统计、甘特图工具是公用的，
新增算法只需实现“就绪队列如何取进程、时间片如何推进”这一部分逻辑。
