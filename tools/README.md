# 工具链

纯 C11 自研工具，全部源码在本目录，**无第三方依赖**（除 zlib）。

## 构建

```bash
make            # 默认输出到 ./build
make list       # 查看产物
make clean
```

需要 `clang`（或 `gcc`）+ `zlib`。已在 aarch64 / Termux 上验证：`make -B` 全量通过（仅告警）。

## 工具清单

### VFS 索引层（`.blc` / `.chk`）

| 工具 | 作用 |
| --- | --- |
| `vfsfull` | `.blc` 全解析：头部、chunk 表、文件条目（含 `codeVersion > 3` 的 8 字节 tag 修正） |
| `blcdump` | 解密体结构 dump（逐字段） |
| `blcfileinfo` | 指定文件条目的详情（偏移/长度/`dataMd5`） |
| `blcsetdata` | 更新文件条目的 `dataMd5` |
| `blcfix` | 更新 chunk 的 `contentMD5` 与尾部 CRC32 |
| `findchunk` | 全局偏移 → 所在 chunk / 块内偏移 |
| `chunknames` | 提取 chunk 名表 |
| `fextract` | 多线程按条目提取文件 |

### `.ab` 容器层

| 工具 | 作用 |
| --- | --- |
| `abtool` | `entropy` / `strings` / `nodes` / `floats` / `u16` / `f32` / `find` / `swap`（等长替换） |
| `abpack` | **自包含 `.ab` 重建器**：解析 → 改 → 重压 → 重建，支持 `min`（最小改动）与 `pad`（布局不变）两种模式 |
| `abverify` | 重建结果读回校验（块表 + 目标区比对） |

### 压缩

| 工具 | 作用 |
| --- | --- |
| `lz4invcomp4` | LZ4Inv 压缩器（与游戏解压器 0 字节差异） |
| `lz4invtest` | 压缩 → 解压 → 逐字节比对自测 |

### 图像 / 像素校验（替代 Python 脚本）

| 工具 | 作用 |
| --- | --- |
| `mktga` | 生成纯色 TGA |
| `mkpattern` | 生成棋盘/条纹等测试图案 |
| `pxsample` | PNG/TGA 像素采样 |
| `scanmagenta` | 扫描并统计目标色像素 |
| `colordetect` | 颜色分布检测 |
| `checkrb` | 红/蓝通道校验 |
| `redbox` | 红框标记 |

### 内存

| 工具 | 作用 |
| --- | --- |
| `ms` | 多线程内存模式搜索（`/proc/<pid>/mem`） |

## 关于解密密钥

`.blc` 的 ChaCha20 密钥属于**游戏运行时材料**，本仓库**不提供**，也不应被分发。

`vfsfull` 从环境变量读取：

```bash
VFS_KEY=<64 位十六进制> ./build/vfsfull path/to/file.blc
```

未设置或格式非法时会明确报错退出。也就是说：本目录提供的是**格式与算法的完整实现**，
而**不是**开箱即用的游戏资源解密器。

## 代码约定

- C11，无全局可变状态（`vfsfull` 的缓冲区除外，为性能考虑）。
- 每个工具单一职责、有 `用法` 提示、错误走 `stderr`。
- 不引入 Python / 解释器依赖：像素与图案校验也由 C 工具完成。
