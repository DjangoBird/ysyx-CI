# C4：ELF 文件和链接

## 学习记录

C4 理解 ELF、链接脚本和符号信息。模拟器运行 bin，但调试需要 ELF。

## 实现记录

AM 默认生成 ELF/bin/txt，并把 ELF 传给 ftrace：

```make
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
NPCFLAGS_BASE += --ftrace $(IMAGE).elf
```

## 关键代码与讲解

ftrace 使用 ELF 符号表解析函数名。没有 ELF 时仍能执行程序，但调用关系只能显示地址。

## 改动代码详解

### 为什么在 AM 平台脚本默认传 ELF

```make
NEMUFLAGS_BASE += --ftrace $(IMAGE).elf
NPCFLAGS_BASE += --ftrace $(IMAGE).elf
```

如果每次运行时手动传 ELF，很容易忘记，ftrace 就只能输出地址。把它放到平台脚本后，
`cpu-tests`、`am-tests`、RT-Thread 都默认具备函数符号信息。

### ELF/bin/txt 的调试分工

```text
ELF: 段表、符号表、入口地址
bin: 实际加载执行的裸镜像
txt: objdump 反汇编，方便人工查 PC
```

模拟器执行 bin，ftrace 解析 ELF，人工 debug 查 txt。三者解决的是不同问题，不能
互相替代。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
readelf -S build/dummy-riscv32-nemu.elf
readelf -s build/dummy-riscv32-nemu.elf
```

## Debug 心得

### 场景 1：ftrace 只有地址没有函数名

检查运行参数：

```bash
ps -ef | rg "riscv32|build/top"
```

应包含：

```text
--ftrace build/<program>.elf
```

再检查 ELF 符号：

```bash
readelf -s build/<program>.elf | rg "FUNC|main|_start"
```

如果 ELF 没有函数符号，ftrace 只能输出地址；如果 ELF 有符号但 ftrace 没加载，看
模拟器启动日志是否有 `loaded function symbols`。

### 场景 2：PC 在 `.txt` 中找不到

可能原因：

- 跳到数据段/栈。
- 链接地址和加载地址不一致。
- 使用了旧 `.txt`。
- PC 已经进入异常 handler 或 fail 路径但你查错文件。

排查：

```bash
readelf -S build/<program>.elf
readelf -h build/<program>.elf | rg "Entry"
rg "8000...." build/<program>.txt
```

如果 PC 落在 `.data/.bss`，优先查函数指针、返回地址、跳转立即数。

### 场景 3：第一条指令机器码不一致

对比：

```bash
objdump -d build/<program>.elf | head -40
xxd -g 4 -l 64 build/<program>.bin
```

RISC-V 是小端。`objdump` 中一条 32 位指令在 bin 中会低字节在前。如果四个字节顺序
反了，检查加载函数或 Flash/SRAM 拼字逻辑。

### 场景 4：全局变量地址错

症状：

- 程序能执行指令，但访问全局变量时读到 0 或越界。
- `.bss` 内容异常。

检查：

```bash
readelf -S build/<program>.elf | rg ".data|.bss|.rodata"
readelf -s build/<program>.elf | rg "变量名"
```

确认 `objcopy` 是否把 `.bss` 作为 alloc/contents 处理，裸机环境没有 ELF loader 替你
清零 `.bss`。

### 场景 5：链接地址和模拟器加载地址不一致

判断：

```text
ELF entry = 0x80000000
simulator load addr = 0x80000000
reset PC = 0x80000000
```

三者必须一致。任一不一致，都可能表现为第一条指令错、PC 跑飞、全局变量地址错。
