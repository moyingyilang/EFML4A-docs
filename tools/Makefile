# EFML4A VFS 工具链 —— 纯 C 构建
#
# 依赖: C11 编译器 (clang / gcc) + make + zlib
#   Termux : pkg install clang make zlib
#   Debian : apt install build-essential zlib1g-dev
#
# 用法:
#   make                 构建全部工具到 $(BUILD)（默认 build/）
#   make build/abpack    只构建单个工具
#   make list            列出已构建产物
#   make help            显示本帮助
#   make clean           删除构建目录
#
# 变量:
#   BUILD=out            指定输出目录
#   CC=gcc               指定编译器（默认 clang）
#   CFLAGS="-O2 -g"      覆盖编译参数
CC      ?= clang
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter
BUILD   ?= build

COMMON  = deps.c vfsaes.c

all: $(BUILD)/abpack $(BUILD)/abverify $(BUILD)/lz4invcomp4 $(BUILD)/lz4invtest \
     $(BUILD)/vfsfull $(BUILD)/blcfix $(BUILD)/blcsetdata \
     $(BUILD)/blcdump $(BUILD)/findchunk $(BUILD)/chunknames \
     $(BUILD)/mktga $(BUILD)/pxsample $(BUILD)/scanmagenta $(BUILD)/colordetect $(BUILD)/mkpattern $(BUILD)/checkrb $(BUILD)/redbox $(BUILD)/ms \
     $(BUILD)/fextract $(BUILD)/abtool

$(BUILD):
	@mkdir -p $(BUILD)

# --- 需要 COMMON（deps.c + vfsaes.c）---
$(BUILD)/abpack: abpack.c $(COMMON) | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ abpack.c $(COMMON)
$(BUILD)/abverify: abverify.c $(COMMON) | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ abverify.c $(COMMON)
$(BUILD)/lz4invcomp4: lz4invcomp4.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ lz4invcomp4.c

$(BUILD)/lz4invtest: lz4invtest.c $(COMMON) | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ lz4invtest.c $(COMMON)

# --- 独立 ---
$(BUILD)/vfsfull: vfsfull.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ vfsfull.c -lz
$(BUILD)/blcfix: blcfix.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ blcfix.c
$(BUILD)/blcsetdata: blcsetdata.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ blcsetdata.c
$(BUILD)/blcdump: blcdump.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ blcdump.c
$(BUILD)/findchunk: findchunk.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ findchunk.c
$(BUILD)/chunknames: chunknames.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ chunknames.c
$(BUILD)/mktga: mktga.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ mktga.c
$(BUILD)/abtool: abtool.c $(COMMON) | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ abtool.c $(COMMON)

# --- 需要 pthread / zlib ---
$(BUILD)/ms: ms.c | $(BUILD)
	@$(CC) $(CFLAGS) -pthread -o $@ ms.c
$(BUILD)/fextract: fextract.c | $(BUILD)
	@$(CC) $(CFLAGS) -pthread -o $@ fextract.c
$(BUILD)/mkpattern: mkpattern.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ mkpattern.c

$(BUILD)/checkrb: checkrb.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ checkrb.c -lz

$(BUILD)/redbox: redbox.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ redbox.c -lz

$(BUILD)/scanmagenta: scanmagenta.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ scanmagenta.c -lz

$(BUILD)/colordetect: colordetect.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ colordetect.c -lz

$(BUILD)/pxsample: pxsample.c | $(BUILD)
	@$(CC) $(CFLAGS) -o $@ pxsample.c -lz

help:
	@sed -n '2,17p' Makefile

list:
	@echo "输出目录: $(BUILD)"
	@ls -1 $(BUILD) 2>/dev/null || echo "(未构建)"

clean:
	rm -rf $(BUILD)
.PHONY: all list help clean
