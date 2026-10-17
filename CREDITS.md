# 致谢 / Credits & Acknowledgements

EFML4A（Arknights: Endfield Mod Loader for Android）能够完成 **Arknights: Endfield 1.6.4
VFS（.blc / .chk / .ab）格式的逆向与工具链实现**，离不开以下开源项目与社区的公开成果。
本项目**参考、复用或移植**了它们的方法论、格式定义与算法实现。谨此致谢。

> 本项目采用「**clean-room 参考 + 独立实现**」的方式：
> 参考社区公开的格式描述与算法，在 **C / C++ / C#(.NET)** 中**独立重写**，
> 并针对 1.6.4 的差异（`codeVersion > 3` 的 8 字节 tag、`bswap32` 字节序、
> `ROR16` 行列置换等）做了**独立发现与修正**。

---

## 一、VFS / .ab / UnityFS 格式参考实现

### 1.1 EndfieldStudio
- **仓库**：https://github.com/EIHRTeam/EndfieldStudio
- **作者**：EIHRTeam 及贡献者
- **本项目使用方式**：
  - 参考 `AnimeStudio/Crypto/VFSUtils.cs` 的 `ReadBlocksInfos` /
    `ReadDirectoryInfos` / `DecryptBlock` / `VFSAES.ExpandKey` /
    `EncryptBlock` / `ShiftRows` / `MixColumns` 的**算法结构**
  - 参考 `AnimeStudio/Crypto/CryptoHelper.cs` 的**密钥常量**
  - 参考 `FileReader` / UnityFS 解析的**资源读取流程**
- **★ 独立发现**：其 `VfsParser.cs` 对 `codeVersion > 3` 的记录**只读 4 字节 tag**，
  这在 **1.6.4 上会失败**。本项目**独立发现该处应为 8 字节（2 × i32）**并修正，
  这是本项目支持 1.6.4 的关键。

### 1.2 AnimeStudio
- **仓库**：https://github.com/Escartem/AnimeStudio
  （亦见 https://github.com/Variante/AnimeStudio 社区分叉）
- **作者**：Escartem 及贡献者
- **本项目使用方式**：
  - 参考 VFS 头部/块表/目录**解扰公式**
  - 参考 `VFSAES` 的**自定义 SBox + CBC-like keystream 派生**
  - `DecryptBlock` 的**稀疏路径**（`len > 256` 时按 `step` gather/scatter）
  - 本项目 C 实现 `tools/vfs/vfsaes.c` 与其做了**逐字节交叉验证**

### 1.3 FkArkEnd
- **仓库**：https://github.com/shiikwi/FkArkEnd
- **作者**：shiikwi
- **本项目使用方式**：参考 `.blc` 解密流程、`.chk` 解包思路、
  `manifest.hgmmap` / `.lua` 定位方法
- **★ 独立发现**：`.blc` 使用 ChaCha20，**counter 恒为 1**（不从数据读取），
  nonce 为前 12 字节

### 1.4 fluffy-dumper
- **仓库**：https://github.com/Escartem/fluffy-dumper
- **作者**：Escartem
- **本项目使用方式**：参考 UnityFS 资源批量导出与清单生成思路

### 1.5 Mazoku
- **仓库**：https://github.com/MhmRdd/mazoku
- **作者**：MhmRdd
- **本项目使用方式**：**仅参考方法论**（attestation 拦截思路）
- **⚠️ 本项目不实现、不包含任何 ACE 绕过代码**（见 `SECURITY.md` 合规红线）

### 1.6 AssetStudio / AssetRipper 生态
- https://github.com/Perfare/AssetStudio
- https://github.com/AssetRipper/AssetRipper
- **本项目使用方式**：参考 UnityFS / SerializedFile / Texture2D 的**通用格式定义**

---

## 二、编解码与压缩

### 2.1 astc-encoder（ARM）
- **仓库**：https://github.com/ARM-software/astc-encoder
- **作者**：ARM Limited 及贡献者
- **许可证**：Apache License 2.0
- **本项目使用方式**：**从源码在 Android/ARM64 本地编译**（`astcenc v5.7.0`），
  作为 ASTC 纹理编解码工具（4×4 / 5×5 / 6×6），
  通过 CLI 与 `dlopen`（`tools/vfs/astcenc_dl.c`）两种方式调用

### 2.2 LZ4
- **仓库**：https://github.com/lz4/lz4
- **作者**：Yann Collet
- **许可证**：BSD 2-Clause
- **本项目使用方式**：参考 **LZ4 块格式**；本项目独立实现了标准 LZ4 与游戏变体
  **LZ4Inv**（token 位交错 `0b00110011`/`0b11001100`，offset **大端**）的
  **压缩器与解压器**（`tools/vfs/lz4*.c`）

### 2.3 ChaCha20
- **参考**：RFC 8439
- **本项目使用方式**：独立实现 ChaCha20 块函数

### 2.4 AES
- **参考**：FIPS-197
- **本项目使用方式**：独立实现 AES-128 加密块，配合游戏**自定义 SBox** 与**自定义 IV 派生**

---

## 三、Android / 注入基础设施

### 3.1 LSPosed
- **仓库**：https://github.com/LSPosed/LSPosed
- **许可证**：GPL-3.0
- **本项目使用方式**：参考 Xposed 模块的**加载与 Hook 生命周期**
  （`android/lsposed-app/`）

### 3.2 Zygisk Next
- **仓库**：https://github.com/Dr-TSNG/ZygiskNext
- **作者**：Dr-TSNG
- **许可证**：GPL-3.0
- **本项目使用方式**：参考 Zygisk 模块的**注入与进程隔离模型**

### 3.3 KernelSU
- **仓库**：https://github.com/tiann/KernelSU
- **作者**：weishu (tiann) 及贡献者
- **许可证**：GPL-2.0
- **本项目使用方式**：作为**测试环境的 root 方案**（研究用途）

### 3.4 Vulkan Layer（本项目自研）
- **参考**：https://github.com/KhronosGroup/Vulkan-Loader（Apache-2.0）
  与 Khronos Vulkan Layer 规范

---

## 四、构建与工具链

| 项目 | 用途 | 许可证 |
|---|---|---|
| [Clang/LLVM](https://github.com/llvm/llvm-project) | C/C++ 编译 | Apache-2.0 with LLVM Exceptions |
| [.NET SDK](https://github.com/dotnet/sdk) | C# 参考工具编译 | MIT |
| [Termux](https://github.com/termux/termux-app) | Android 开发环境 | Apache-2.0 / GPL-3.0 |
| [Gradle](https://github.com/gradle/gradle) | Android 构建 | Apache-2.0 |
| [Android NDK](https://github.com/android/ndk) | 原生工具链 | Apache-2.0 |

---

## 五、特别致谢

- **Endfield 研究社区**：所有公开分享 VFS / UnityFS 格式发现的研究者。
- **AnimeStudio / EndfieldStudio 的作者**：其 `VFSUtils.cs` 与 `CryptoHelper.cs`
  是本项目格式理解的主要参考。
- **astc-encoder 团队（ARM）**：提供了高质量、可本地编译的 ASTC 编解码器。
- **LZ4 的作者 Yann Collet**：其格式文档与实现是本项目压缩器的基础。

---

## 六、本项目的独立贡献

在参考上述成果的基础上，本项目**独立发现并实现**了：

1. **`codeVersion > 3` 的 8 字节 tag 修正**——公开工具无法支持 1.6.4 的根因
2. **全部混淆/逆混淆公式的完整推导与多值域往返验证**（header / blocks / nodes）
3. **`blocksCount` / `nodesCount` 的字节序机制**（小端读 + `bswap32` + `ror18`）
4. **`encFlags` 逆公式**（`ROR16` + `a1 = 高位 ^ a0`）
5. **VFSAES 的对合性**（加密 = 解密，同一 XOR keystream）
6. **LZ4Inv 压缩器**（往返验证，与游戏解压器 0 字节差异）
7. **ASTC 纹理格式的实测确认**（4×4 / 5×5 / 6×6，非 BC7）
8. **完全自包含的纯 C `.ab` 重建器**（`tools/vfs/abpack.c`）
9. **对 `tools/vfs/vfsaes.c` 中 ShiftRows 转置错误的定位与修正**

---

## 七、致谢的维护

如果您认为本项目的某个部分使用了您的成果而未在此列出，
请提交 Issue 或 PR，我们会立即补充。

**本文件随项目演进而更新。**
