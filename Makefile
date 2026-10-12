# ===========================================================================
#  Makefile —— 使用 make 编译本实验（Linux / macOS / 已安装 make 的 Windows）
#  如果没有 make，可以直接运行 build.ps1（Windows + PowerShell）。
# ===========================================================================
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Iinclude
SRCDIR  = src
BINDIR  = bin

ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif

TARGETS = $(BINDIR)/fcfs$(EXE) $(BINDIR)/rr$(EXE) $(BINDIR)/mlfq$(EXE) $(BINDIR)/banker$(EXE)

all: $(TARGETS)

$(BINDIR)/%$(EXE): $(SRCDIR)/%.c $(SRCDIR)/common.c include/sched.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SRCDIR)/common.c $<

clean:
	rm -f $(BINDIR)/fcfs$(EXE) $(BINDIR)/rr$(EXE) $(BINDIR)/mlfq$(EXE) $(BINDIR)/banker$(EXE)

.PHONY: all clean
