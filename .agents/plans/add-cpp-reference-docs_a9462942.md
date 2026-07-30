---
name: add-cpp-reference-docs
overview: 将 cxx-frontend 和 aburi 两个 C++ 编译器前端参考项目引入 reference/ 目录，并同步更新 AGENTS.md、mcc/ARCHITECTURE.md、mcc/.todo/cpp-shared-backend.md 三份文档中的参考资源列表。
todos:
  - id: clone-ref-repos
    content: 创建 reference/ 目录并拉取 cxx-frontend（robertoraggi/cplusplus）和 aburi（serjective/aburi）参考源码
    status: completed
  - id: update-docs
    content: 更新 AGENTS.md、projects/mcc/ARCHITECTURE.md、projects/mcc/.todo/cpp-shared-backend.md 三份文档中的 C++ 参考资源条目
    status: completed
    dependencies:
      - clone-ref-repos
---

## 用户需求
将 cxx-frontend 和 aburi 两个 C++ 编译器前端参考项目纳入项目的参考资源体系：
1. 将两个项目的源码拉取到 `reference/` 目录下
2. 更新项目文档，使 C++/m++ 实现有明确的社区参考可供研究

## 涉及文件

### 源码拉取
- `reference/cxx-frontend/`：`git clone https://github.com/robertoraggi/cplusplus.git`（C++23 编译器前端，BSL-1.0 许可）
- `reference/aburi/`：`git clone https://github.com/serjective/aburi.git`（现代 C/C++ 编译器前端，Apache-2.0+MIT 许可）

### 文档更新
- `AGENTS.md` 第 352-369 行：参考树表格新增 cxx-frontend 和 aburi 条目；社区资源-编译器设计补充 C++ 前端参考
- `projects/mcc/ARCHITECTURE.md` 第 338-351 行：Reference Trees 表格新增两行
- `projects/mcc/.todo/cpp-shared-backend.md` 第 182-185 行：参考实现段落补充 cxx-frontend 和 aburi

## 技术方案

### 整体策略
纯文档更新 + git clone 操作，无代码变更。`reference/` 已被 `.gitignore` 排除，clone 的源码不会被提交。

### 实现步骤

#### 1. 拉取参考源码
在仓库根目录创建 `reference/` 目录，依次 clone 两个仓库：
- `git clone https://github.com/robertoraggi/cplusplus.git reference/cxx-frontend`
- `git clone https://github.com/serjective/aburi.git reference/aburi`

两个 clone 操作完全独立，可并行执行。

#### 2. 更新 AGENTS.md（§6.2 参考资源）

**位置 A**（第 359 行后，参考树表格）：新增两行：
```
| `reference/cxx-frontend/` | m++ C++ 前端参考（C++23 词法/语法/语义解析、AST 设计） |
| `reference/aburi/` | m++ C++ 前端参考（lexer/parser/preprocessor/ast/constexpr、Itanium ABI 降级） |
```

**位置 B**（第 364 行，编译器设计社区资源）：补充 C++ 前端：
```
- **编译器设计（C++）**：cxx-frontend（C++23 完整前端，已 vendored）、aburi（C/C++ 前端全流程，已 vendored）
```

#### 3. 更新 projects/mcc/ARCHITECTURE.md（§9 Reference Trees）

在第 346 行后新增两行：
```
| `reference/cxx-frontend/` | HEAD | `master` | m++ C++ frontend design reference |
| `reference/aburi/` | v0.1.1 | `master` | m++ C++ frontend full-pipeline reference |
```

#### 4. 更新 projects/mcc/.todo/cpp-shared-backend.md

在第 184 行后新增两条参考：
```
    - cxx-frontend（robertoraggi/cplusplus）：独立 C++23 前端，词法/语法/语义完整流程，AST 节点设计参考
    - aburi（serjective/aburi）：C/C++ 前端全流程（lex→preprocess→parse→ast→constexpr→ast2llvm），类/继承/虚派发/模板/异常/Itanium C++ ABI 参考
```

### 影响范围
- 仅文档修改和 `reference/` 目录新增，无代码变更
- `reference/` 被 `.gitignore` 排除，不影响 git 状态
- 不影响任何构建流程或编译器行为
