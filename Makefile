# GZG 语言解释器构建脚本
#   make            编译 build/gzg(.exe)
#   make test       运行全部黑盒测试
#   make bench      运行性能测试并生成报告
#   make coverage   带覆盖率插桩编译并运行测试，输出 gcov 行覆盖率
#   make stress     GC 压力模式（每次分配都触发回收）下运行测试
#   make clean

ifeq ($(origin CC),default)
  CC = gcc
endif
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter -D__USE_MINGW_ANSI_STDIO=1
LDLIBS  = -lm
SRC     = $(wildcard src/*.c)

ifeq ($(OS),Windows_NT)
  EXE    = build/gzg.exe
  LDLIBS += -lshell32
  PYTHON ?= py -3
else
  EXE    = build/gzg
  PYTHON ?= python3
endif

.PHONY: all test bench coverage stress clean

all: $(EXE)

$(EXE): $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

test: $(EXE)
	$(EXE) tests/run_tests.gzg

bench: $(EXE)
	$(PYTHON) bench/run_bench.py

stress:
	@mkdir -p build/stress
	$(CC) $(CFLAGS) -DGC_STRESS -o build/stress/gzg $(SRC) $(LDLIBS)
	build/stress/gzg tests/run_tests.gzg --gzg build/stress/gzg

coverage:
	@rm -rf build/cov && mkdir -p build/cov
	cd build/cov && $(CC) -std=c99 -O0 --coverage -D__USE_MINGW_ANSI_STDIO=1 -o gzg ../../src/*.c $(LDLIBS)
	build/cov/gzg tests/run_tests.gzg --gzg build/cov/gzg
	@cd build/cov && for f in ../../src/*.c; do printf "%-14s " $$(basename $$f); gcov -n -o . $$f | grep -m1 "Lines executed"; done
	@cd build/cov && for f in ../../src/*.c; do gcov -n -o . $$f | grep -m1 "Lines executed"; done | \
		awk -F'[:% ]+' '{ hit += $$3 * $$5 / 100; all += $$5 } END { printf "合计           Lines executed:%.2f%% of %d\n", hit * 100 / all, all }'

clean:
	rm -rf build
