# loongarch64 PIE / GOT-PIC 专项（task#10 边界派生，后置）

**状态**：🔶 待排（2026-08-07 复查：mt/as/ld 端 PIE 支持已全，需 loongarch64 ld.so 交叉编译以验证运行时）

## 背景
task#10 阶段一（纯调研）结论：loongarch64 PC-relative 静态链接**无真实缺陷**，
LA64 LO12 公式 `(S+A)&0xFFF` 不含 P、不需 HI20 配对，结构性免疫 riscv64 跨
reloc-section HI20 配对缺陷。但调研中暴露两个**基础设施/场景边界**非缺陷项：

## 边界项
1. **PIE / 动态链接**：`-pie` 产物运行失败 `/lib/ld-meuos.so.1: Invalid ELF image`
   —— loongarch64 动态链接器（ld-meuos.so）未部署，属**基础设施边界**，非
   PCALA reloc 问题。静态链接是验证基线。
2. **GOT-PC(75/76) 在 PIC 下**：mcc 静态链接不发 GOT_PC（-fPIC+静态也不触发
   GOT），该深面在 PIC 场景**未充分验证**，属后续 PIC 专项范围。

## 依赖
- 需先部署 loongarch64 动态链接器（ld-meuos.so）才能验证 PIE。
- 与 mt/ld 的 GOT reloc 路径、PIC 代码生成（mcc loongarch64 `-fPIC`）联动。

## 与 riscv64-lod12-anchor 的关系
riscv64 那条是 **真缺陷**（LO12 用局部标签需 HI20 配对、跨 section 配对失败）；
loongarch64 结构上豁免，本专项**不等价于** riscv64 重构，仅覆盖 PIC/动态链接场景。
