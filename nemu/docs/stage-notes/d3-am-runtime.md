# D3：运行时环境与 AM

## 学习记录

D3 的重点是理解 AM 如何把普通 C 程序放到裸机环境运行。NEMU 不直接运行 ELF，而是
运行 AM 生成的 bin；AM 负责入口、栈、`main()` 调用和退出。

## 实现记录

关键文件：

```text
abstract-machine/scripts/platform/nemu.mk
abstract-machine/am/src/riscv/nemu/start.S
abstract-machine/am/src/platform/nemu/trm.c
```

实现要点：

- 镜像链接到 `0x80000000`。
- NEMU 从 `RESET_VECTOR` 加载 bin。
- `main()` 返回后通过 `halt()` 触发 `ebreak`。
- `run-batch` 用于自动运行客户程序。

## 关键代码与讲解

```make
LDFLAGS += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
NEMUFLAGS_BASE += -l $(shell dirname $(IMAGE).elf)/nemu-log.txt
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
```

讲解：

- `_pmem_start` 必须和 NEMU 物理内存起点一致。
- `-l` 固定日志位置，便于检查 trace。
- `--ftrace` 把 ELF 传给 NEMU，用于符号解析。

## 改动代码详解

### `run` 和 `run-batch` 的区别

```make
run: insert-arg
	$(MAKE) -C $(NEMU_HOME) ISA=$(ISA) run ARGS="$(NEMUFLAGS_BASE)" IMG=$(IMAGE).bin

run-batch: insert-arg
	$(MAKE) -C $(NEMU_HOME) ISA=$(ISA) run ARGS="$(NEMUFLAGS_BASE) -b" IMG=$(IMAGE).bin
```

两条规则的核心差别是 `-b`。没有 `-b` 时，NEMU 进入 sdb，需要手动输入 `c`；
有 `-b` 时，NEMU 直接执行客户程序。PA 测试和回归脚本应该优先使用 `run-batch`，
否则会误以为程序卡住。

### `insert-arg`：裸机程序的参数注入

```make
MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN)
CFLAGS += -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

insert-arg: image
	@$(PYTHON) $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin \
	  $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"
```

`am-tests` 需要 `mainargs=t/d/y/i` 选择测试项。裸机程序没有 Linux 那样的 argv，
所以 AM 先在镜像里放一段占位字符串，构建完成后再把真实参数写进 bin。这个设计让
同一个 AM 程序可以在 NEMU/NPC 上保持一致的参数传递方式。

### ELF/bin/txt 三件套

```make
image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents \
	  -O binary $(IMAGE).elf $(IMAGE).bin
```

执行使用 `.bin`，ftrace 使用 `.elf`，人工排查使用 `.txt`。这三者的分工很重要：
如果只保留 bin，程序能跑但很难定位函数和 PC；如果只看 ELF 而不看 bin，又无法确认
实际加载到模拟器的字节是否正确。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
make ARCH=riscv32-nemu ALL="add load-store div" run-batch
```

## Debug 心得

- `run` 会进入 sdb，需要手动输入 `c`；自动测试用 `run-batch`。
- 如果程序一开始就跑飞，检查链接地址、`RESET_VECTOR` 和 `load_img()`。
- 如果 ftrace 没有函数名，检查是否传入了 `.elf`。
