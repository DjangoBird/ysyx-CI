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

- ftrace 没函数名，先检查 `--ftrace $(IMAGE).elf`。
- PC 跑飞时，用 `.txt` 找反汇编位置。
- 链接地址错误会导致第一条指令就不对。
