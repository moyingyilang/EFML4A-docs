# EFML4A 工具链

**25 个纯 C 工具 + 12 个 zygisk payload，零 Python。**

---

## 构建

```bash
make            # 全部构建到 build/
make list       # 看构建了什么
make clean
```

**依赖：** `clang`（或 gcc）、`zlib`、`pthread`

**在 Termux 上：**
```bash
pkg install clang zlib
```

---

## 工具速查

### 格式解析

| 工具 | 用途 |
|---|---|
| `vfsfull` | 解析 `.blc`（1.6.4 唯一正确实现） |
| `abtool` | 解析 `.ab` 的头/块表/节点表 |
| `blcdump` | `.blc` 结构 + CRC 校验 |
| `findchunk` | 全局偏移 → chunk 定位 |
| `chunknames` | 提取 chunk 名列表 |

### 压缩

| 工具 | 用途 |
|---|---|
| `lz4invcomp4` | LZ4Inv v4 压缩器 |
| `lz4invtest` | 压缩器自测（往返验证） |

### 容器重建

| 工具 | 用途 |
|---|---|
| `abpack` | 重建 `.ab`（`min` / `pad` 双模式） |
| `abverify` | 读回验证 + 全流导出 |

### `.blc` 写入

| 工具 | 用途 |
|---|---|
| `blcfix` | 更新 chunk `contentMD5` + CRC32 |
| `blcsetdata` | 更新 file `dataMd5` + CRC32 |
| `blcredirect` | 重定向 file entry（chunkName/off/len） |

### 运行时

| 工具 | 用途 |
|---|---|
| `mempatch` | 内存搜索替换（`/proc/<pid>/mem`） |
| `ms` | 多线程内存搜索 |
| `fextract` | 多线程 `pread` 提取（3.5s / 1075 文件） |

### 检测辅助

| 工具 | 用途 |
|---|---|
| `binsearch` | 大文件二进制搜索（流式） |
| `ent` | 熵分析（判断加密/压缩） |
| `crackx` | XOR 破解 + 已知明文攻击 |
| `colordetect` | 5 种纯色检测 |
| `checkrb` | 纯红/纯蓝检测 |
| `redbox` / `yellowbox` | 颜色空间范围 |
| `scanmagenta` | 高饱和色扫描 |
| `mkpattern` | 生成测试图案 |
| `mktga` | 生成纯色 TGA |
| `pxsample` | PNG 像素采样 |
| `selftest` | 工具链回归测试 |

---

## zygisk payload

| payload | 用途 |
|---|---|
| `payload-vktex.c` | 观测纹理上传（3 hook，可作基础） |
| `payload-vkinject.c` | 注入版本 |
| `payload-vksafe.c` | 安全只读探测 |
| `payload-libcprobe.c` | libc 探测 |
| `payload-vfsprobe.c` | VFS 探测 |
| `payload-meshdump.c` | Mesh 导出 |

**编译：**
```bash
clang -O2 -fPIC -shared -nostdlib -o libefml4a_xxx.so payload-xxx.c
```

**部署：** 见 [docs/E1](../docs/E1-runtime.md)

---

## 完整流程

见 [docs/C1 · 离线写入链路](../docs/C1-write-chain.md)

---

## 文档

- [文档索引](../docs/00-INDEX.md)
- [A5 · 纹理与流偏移](../docs/A5-textures.md) —— 最实用
- [A6 · VFS 密钥获取](../docs/A6-key-extraction.md)
- [C2 · 阻塞与失败记录](../docs/C2-blockers.md) —— 为什么最后还是不行

---

## 许可

LGPL-3.0（见仓库根目录 LICENSE）
