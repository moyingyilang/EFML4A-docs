# 语言政策 / Language Policy

## 最终实现语言

**本项目（含所有分立项目）的最终实现语言必须是 C 系语言：**

| 允许 | 用途 |
|---|---|
| **C**（C11/C17） | 核心工具链（VFS 解析、压缩、加密、重建） |
| **C++**（C++17/20） | 需要更复杂抽象的部分（GUI、纹理处理） |
| **C# / .NET** | 工具类应用（含 GUI）、跨平台桌面工具 |

**❌ 禁止**：Python、JavaScript/TypeScript（作为实现语言）、
以及任何非 C 系语言的**实现代码**。

> 注：构建脚本可使用 **Shell**（POSIX/bash）；
> 文档可使用 Markdown。

## 现状（截至 Round 153）

```
C 工具:   43 个（tools/vfs/*.c）
C# 工具:  7 个（ref/*，参考工具）
C++:      astcenc v5.7.0（ARM 源码编译）
Python:   0（已全链路清零）
```

`.gitignore` 已排除 `*.py` / `*.pyc` / `__pycache__/`。
