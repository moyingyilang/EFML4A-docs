# EFML4A · 公开技术文档

**Android 端《明日方舟：终末地》mod 加载器研究 —— 格式逆向与工具链**

[文档索引](docs/README.md) · [致谢](CREDITS.md) · [安全与合规](SECURITY.md) · [语言政策](LANGUAGE.md)

> 本仓库是 EFML4A 研究的**公开文档部分**：把格式逆向结论与自研工具开放给人类阅读与复用。
> **不包含**逐轮实验日志、设备数据、密钥材料与任何游戏资源。

---

## 一、这是什么

PC 上的「换模型」mod 生态已经成熟了十几年：3DMigoto 用一个代理 DLL 劫持 Direct3D，
在图形 API 边界把游戏要画的顶点/索引缓冲换掉；EFMI 是这条脉络上针对《终末地》的最新实现。

**Android 上这套东西无法直接搬运**：没有 DXGI，图形栈是 Vulkan / GLES；进程隔离 + SELinux 让注入需要 root；
目标是活跃运营的 Unity 游戏，资源在 VFS 容器里且有版本更新与完整性校验。

EFML4A 回答的问题是：**在 Android 上应该怎么做？** 本仓库公开的就是这个问题的研究产出：
容器格式逆向结论 + 我们自己写的分析/重建工具。

## 二、四个项目的关系

| 项目 | 角色 | 语言 | 许可 | 可见性 |
| --- | --- | --- | --- | --- |
| **EFML4A** | 主研究仓：路线评估、格式逆向、实验记录、安卓侧骨架 | C / C++ / Rust / Kotlin | GPL-3.0 | private |
| **EFML4A-repack** | 从主仓拆出的 VFS/`.ab` 解析与重建库（可复用） | C11 | LGPL-3.0 | private |
| **EFML4A-gui** | GUI / TUI 前端（依赖 repack） | C++ / C#(.NET 10) | GPL-3.0 | private |
| **EFML4A-docs**（本仓库） | **公开**：技术文档 + 自研工具源码 | Markdown / C11 | GPL-3.0 | **public** |

拆分的理由：让「格式与工具」可以被单独引用和审查，而研究过程与实验环境保持私有。

## 三、已经确证的技术结论

> 全部结论均来自实机实测，详见 [`docs/`](docs/README.md)。以下只列骨架。

| 层级 | 结论 |
| --- | --- |
| **VFS 索引（`.blc`）** | ChaCha20（`nonce` = 前 12 字节，`counter` = 1）+ 尾部 CRC32；`codeVersion > 3` 时 tag 是 **8 字节**（公开工具只读 4 字节 ⇒ 对 1.6.4 全部失效） |
| **数据块（`.chk`）** | 文件名 = `.blc` chunk 头前 16 字节的 MD5；内容 MD5 在 chunk 头第 2 段 |
| **`.ab` 容器** | 48 字节混淆头 + 混淆块表（10 字节/块，混合字节序）+ 节点表 + LZ4Inv 数据块；读/写逆公式均已验证 |
| **AES 层** | VFSAES 的自定义 CBC-like keystream **加密 = 解密**（对合），大幅简化写入 |
| **压缩** | LZ4Inv（token 隔位 + 大端 offset），自研压缩器与游戏解压器 **0 字节差异** |
| **纹理** | ASTC 4x4 / 5x5 / 6x6；命名规律 `T_<类别>_<角色>_face_<编号>_<类型>` |
| **Unity Mesh** | 顶点是**压缩格式**（TypeTree 含 `m_Compression` / `m_PackedBitVector`），这是「模型换了但扭曲」的根因 |

## 四、文档

| # | 文档 | 内容 |
| --- | --- | --- |
| 01 | [项目定位与研究问题](docs/01-overview.md) | 术语、目标设备、非目标 |
| 02 | [四条路线决策矩阵](docs/02-route-matrix.md) | Vulkan 层 / IL2CPP hook / AssetBundle / frida 的取舍 |
| 03 | [VFS 容器](docs/03-vfs-container.md) | `.blc` / `.chk` 结构与解密 |
| 04 | [`.ab` 容器](docs/04-ab-container.md) | 头部混淆、块表、节点表 |
| 05 | [写入路径](docs/05-vfs-write-path.md) | 读 → 改 → 重压 → 重建 → 校验 |
| 06 | [Unity 压缩 Mesh](docs/06-unity-compressed-mesh.md) | 顶点压缩格式剖析 |
| 07 | [EFMI `.buf` 格式](docs/07-efmi-buf-format.md) | PC 侧 mod 资产的顶点/索引/UV/权重布局 |
| 08 | [Mod 包格式规范](docs/08-mod-format.md) | 与后端解耦的声明式 IR（v0） |
| 09 | [威胁模型与合规](docs/09-threat-model.md) | 红线、检测面、许可证兼容性 |
| 10 | [研究现状](docs/10-status.md) | 已确证 / 未解决 / 下一步 |

## 五、工具

纯 C11、无第三方依赖（除 zlib）。每个工具的参数、示例与端到端流程见
[`tools/README.md`](tools/README.md)。

**依赖**

| 依赖 | Termux | Debian / Ubuntu |
| --- | --- | --- |
| C11 编译器 | `pkg install clang` | `apt install build-essential` |
| make | `pkg install make` | 同上 |
| zlib | `pkg install zlib` | `apt install zlib1g-dev` |

**构建**

```bash
cd tools
make                 # 构建全部工具到 ./build
make list            # 列出产物
make help            # 构建说明
make build/abpack    # 只构建单个工具
make BUILD=out CC=gcc
make clean
```

**工具速查**

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| `vfsfull` 🔑 | `vfsfull <in.blc>` | `.blc` 全解析（含 8 字节 tag 修正） |
| `blcdump` 🔑 | `blcdump <in.blc>` | 解密体逐字段 dump |
| `chunknames` 🔑 | `chunknames <in.blc>` | chunk 名表 |
| `findchunk` 🔑 | `findchunk <in.blc> <global_offset>` | 全局偏移 → chunk / 块内偏移 |
| `blcsetdata` 🔑 | `blcsetdata <in.blc> <out.blc> <filename> <md5>` | 更新文件条目 `dataMd5` |
| `blcfix` 🔑 | `blcfix <in.blc> <out.blc> <chk_md5> [md5Name]` | 更新 `contentMD5` + CRC32 |
| `fextract` | `fextract <src> <list> <outdir> [threads]` | 多线程按 `off len name` 提取 |
| `abtool` | `abtool <in.ab> [--dump]` | 校验头、解析块表与节点表 |
| `abpack` | `abpack <in.ab> <out.ab> <streamOff> <new.bin> [min\|pad]` | **自包含 `.ab` 重建器** |
| `abverify` | `abverify <in.ab> [stream] [target] [expect]` | 重建后读回校验 |
| `lz4invtest` | `lz4invtest <in.bin>` | 压缩→解压→逐字节比对自测 |
| `lz4invcomp4` | `lz4invcomp4 <in> <out>` | LZ4Inv 压缩器（v4） |
| `ms` | `ms <pid> <hexpat> [max] [threads]` | 多线程内存模式搜索 |
| `mktga` / `mkpattern` | `mktga <out.tga> <w> <h> <r> <g> <b>` | 生成纯色 / 测试图案 TGA |
| `pxsample` / `scanmagenta` / `colordetect` / `checkrb` / `redbox` | `<in.png>` | 截图像素校验（替代 Python 脚本） |

🔑 = 需要 `VFS_KEY` 环境变量（64 位十六进制）。

> **关于密钥**：`.blc` 的解密密钥属于游戏运行时材料，本仓库**不提供**。
> 上述 🔑 工具从环境变量 `VFS_KEY` 读取，缺失或格式非法时立即报错退出。
> 因此本仓库提供的是「格式与算法」的完整实现，**不能**直接解密任意游戏数据。

**最短上手**

```bash
cd tools && make
export VFS_KEY=<64 位十六进制>
./build/vfsfull path/to/game.blc | head -40
```

## 六、合规红线

```
✅ 仅研究用途  ✅ 等长替换（可随时还原）  ✅ 不改游戏数值/存档
❌ 不用于竞技作弊  ❌ 不散布游戏资源  ❌ 不实现 ACE / 反作弊绕过
```

本项目**不涉及**修改数值、经济、掉落、战斗结果，也不提供任何绕过正版校验或反作弊的实现。
详见 [`docs/09-threat-model.md`](docs/09-threat-model.md) 与 [`SECURITY.md`](SECURITY.md)。

## 七、许可证

[GPL-3.0](LICENSE)。工具与文档同源，均来自 EFML4A 主仓。
第三方参考实现的归属见 [`CREDITS.md`](CREDITS.md)。

---

<sub>本项目为研究性质，与 Hypergryph / 鹰角网络无隶属关系，不使用其商标与资源。</sub>
