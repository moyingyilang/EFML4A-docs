# 文档索引

本目录是 EFML4A 的**公开技术文档（节选版）**。完整研究记录、逐轮实验日志与设备数据不公开。

> **关于编号**：正文里的章节号（如 `## 50.4`、`## 52.9`）沿用原始研究日志，
> 便于与私有仓库逐条对照；文件名已按主题重排。

## 阅读路径

| 你想知道 | 读哪几篇 |
| --- | --- |
| 这个项目到底在做什么 | [01](01-overview.md) → [02](02-route-matrix.md) |
| 游戏资源容器长什么样 | [03](03-vfs-container.md) → [04](04-ab-container.md) → [05](05-vfs-write-path.md) |
| 模型 / 贴图怎么换 | [06](06-unity-compressed-mesh.md) → [07](07-efmi-buf-format.md) |
| mod 包格式如何设计 | [08](08-mod-format.md) |
| 风险、合规、检测面 | [09](09-threat-model.md) |
| 现在做到哪一步了 | [10](10-status.md) |

## 清单

| # | 文档 | 内容 | 原始编号 |
| --- | --- | --- | --- |
| 01 | [项目定位与研究问题](01-overview.md) | 研究问题、术语表、目标设备、非目标 | `01` |
| 02 | [四条路线决策矩阵](02-route-matrix.md) | Vulkan 隐式层 / IL2CPP hook / AssetBundle / frida 的可行性、工程量、检测风险 | `07` |
| 03 | [VFS 容器](03-vfs-container.md) | `.blc` 全字段、ChaCha20 参数、8 字节 tag 修正、CRC32 | `50` |
| 04 | [`.ab` 容器](04-ab-container.md) | 48 字节头、混淆块表、节点表、LZ4Inv 数据块 | `49` |
| 05 | [写入路径](05-vfs-write-path.md) | 读 → 改 → 重压 → 重建 → 校验的完整链路与逆公式 | `52` |
| 06 | [Unity 压缩 Mesh](06-unity-compressed-mesh.md) | `m_Compression` / `m_PackedBitVector` / MinMax / StreamData | `40` |
| 07 | [EFMI `.buf` 格式](07-efmi-buf-format.md) | PC 侧 mod 资产的 VB0/VB1/VB2/IB 布局与不变量 | `28` |
| 08 | [Mod 包格式规范 v0](08-mod-format.md) | 与后端解耦的声明式 IR、校验规则、EFMI 映射表 | `10` |
| 09 | [威胁模型与合规](09-threat-model.md) | 红线清单、反作弊检测面、许可证兼容性、私有期策略 | `08` |
| 10 | [研究现状](10-status.md) | 已确证 / 未解决 / 下一步验证路径 | `56` |

## 约定

- **可复现**：凡结论都给出验证方式（字节级对比、往返测试、CRC 校验）。
- **不夸大**：未能核实的内容标注为推断；已证伪的路线直接写明失败原因。
- **不含密钥**：所有涉及解密密钥的位置一律留空，由读者自行提供（见工具说明）。
