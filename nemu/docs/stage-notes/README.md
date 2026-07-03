# 分阶段学习记录索引

本目录把原总报告 `../c-d-stage-experiment-report.md` 拆成按阶段阅读的学习记录。
总报告保留，用于整体复盘；本目录用于按 D/C/B1 阶段查找实现、运行和 debug 心得。

每份笔记统一包含：

- 学习记录
- 实现记录
- 关键代码与讲解
- 运行方式
- Debug 心得

## D 阶段

- [D1：支持 RV32IM 的 NEMU](d1-rv32im-nemu.md)
- [D2：程序的机器级表示](d2-machine-code.md)
- [D3：运行时环境与 AM](d3-am-runtime.md)
- [D4：RTL minirv NPC](d4-minirv-npc.md)
- [D5：设备和输入输出](d5-device-io.md)
- [D6：D 阶段流片准备](d6-soc-bus-prep.md)

## C 阶段

- [C1：工具和基础设施](c1-infra.md)
- [C2：支持 RV32E 的 NPC](c2-rv32e-npc.md)
- [C3：调试技巧](c3-debug.md)
- [C4：ELF 文件和链接](c4-elf-link.md)
- [C5：异常处理和 RT-Thread](c5-exception-rtthread.md)

## B 阶段

- [B1：总线、AXI-Lite、Access Fault 和性能](b1-bus-axi.md)

