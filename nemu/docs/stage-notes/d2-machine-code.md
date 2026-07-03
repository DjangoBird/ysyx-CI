# D2：程序的机器级表示

## 学习记录

D2 主要学习 C 程序如何变成机器指令。对 NEMU/NPC 来说，关键是能把 ELF、bin、
反汇编、itrace 对起来。

学习重点：

- ELF 保存段、符号和调试相关信息。
- bin 是实际加载到模拟器的裸镜像。
- 反汇编 `.txt` 用于人工定位 PC 对应的指令。

## 实现记录

AM 构建时会同时生成：

```text
build/<name>.elf
build/<name>.bin
build/<name>.txt
```

`.elf` 给 ftrace 和 readelf 使用，`.bin` 给 NEMU/NPC 加载，`.txt` 用于阅读机器级代码。

## 关键代码与讲解

```make
image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents \
	  -O binary $(IMAGE).elf $(IMAGE).bin
```

讲解：

- `objdump -d` 生成反汇编。
- `objcopy -O binary` 去掉 ELF 元信息，只保留可执行镜像内容。
- `.bss` 被设置为 `alloc,contents`，保证裸 bin 中有需要的初始化空间。

## 改动代码详解

### `OBJDUMP` 输出为什么重要

```make
@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
```

这一步不改变程序行为，但提升可调试性。`.txt` 能把 PC、机器码和汇编指令对应起来。
当 itrace 显示某个 PC 跑飞时，可以立刻在 `.txt` 中查到上下文，包括前后指令、函数
边界和跳转目标。

### `OBJCOPY` 为什么要输出裸 bin

```make
@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents \
  -O binary $(IMAGE).elf $(IMAGE).bin
```

NEMU/NPC 不是 Linux ELF loader，它们只把一段二进制字节加载到客户机内存。因此
真正执行的是裸 bin。`-S` 去掉调试信息，`-O binary` 输出连续镜像；
`.bss=alloc,contents` 确保未初始化全局变量在裸镜像中也有对应空间。

### ELF 仍然不能丢

执行用 bin，但 ELF 仍然用于：

- `readelf` 查看段和符号。
- `objdump` 生成反汇编。
- ftrace 解析函数名。

因此 AM 同时保留 ELF、bin 和 txt。

## 运行方式

```sh
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
make ARCH=riscv32-nemu ALL=dummy run-batch
less build/dummy-riscv32-nemu.txt
```

## Debug 心得

- 看到错误 PC 后，先在 `.txt` 中定位指令。
- 再对照 `build/nemu-log.txt` 中 itrace。
- 如果 `.txt` 和 itrace 对不上，检查加载地址和链接脚本。
