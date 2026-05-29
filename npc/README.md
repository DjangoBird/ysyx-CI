# NPC 模拟器（Chisel 版本）

本目录是一个基于 Chisel + Verilator 的 NPC。当前目标是 RV32E 单周期实现，硬件内部已经按取指、译码、执行、访存、寄存器堆和 PC 模块拆开，方便后续继续演进为多周期或流水线。

**先决条件**
- Linux 环境
- 安装工具：`git`, `make`, `g++`, `verilator`, `python3`, `java`, `sbt`
- 如果需要 GUI（NVBoard / SDL 显示），还需安装：`libsdl2-dev` 以及 NVBoard 的相关依赖

示例（Debian/Ubuntu）快速安装：

```bash
sudo apt update
sudo apt install -y git build-essential verilator python3 python3-pip libsdl2-dev openjdk-17-jdk
```

获取代码

```bash
git clone <your-repo-url>
cd ysyx-workbench/npc
```

构建

```bash
make -j$(nproc) all
```

构建流程会先用 Chisel 生成 `build/chisel/top.v`，再由 Verilator 编译出 `build/top`。

运行

```bash
./build/top path/to/img.bin
```

如果不传镜像，程序会从空内存启动。

环境变量（可选）
- `NPC_GUI=1`：启用 NVBoard GUI 输出（若支持）

关键源码位置
- C++ 运行时： [csrc](csrc)
  - [csrc/main.cpp](csrc/main.cpp)
  - [csrc/npc_runtime.h](csrc/npc_runtime.h) / [csrc/npc_runtime.cpp](csrc/npc_runtime.cpp)
  - [csrc/npc_memory.h](csrc/npc_memory.h) / [csrc/npc_memory.cpp](csrc/npc_memory.cpp)
  - [csrc/npc_step.h](csrc/npc_step.h) / [csrc/npc_step.cpp](csrc/npc_step.cpp)
- Chisel 硬件： [src/main/scala/npc](src/main/scala/npc)
  - [src/main/scala/npc/Top.scala](src/main/scala/npc/Top.scala)
  - [src/main/scala/npc/FetchStage.scala](src/main/scala/npc/FetchStage.scala)
  - [src/main/scala/npc/DecodeStage.scala](src/main/scala/npc/DecodeStage.scala)
  - [src/main/scala/npc/ExecuteStage.scala](src/main/scala/npc/ExecuteStage.scala)
  - [src/main/scala/npc/MemoryStage.scala](src/main/scala/npc/MemoryStage.scala)
  - [src/main/scala/npc/RegisterFile.scala](src/main/scala/npc/RegisterFile.scala)
  - [src/main/scala/npc/ProgramCounter.scala](src/main/scala/npc/ProgramCounter.scala)

排查建议
- 如果第一次构建较慢，通常是 sbt 在拉取 Chisel 依赖。
- 如果 Verilator 找不到生成的 `top.v`，先检查 Chisel 生成步骤是否成功完成。


