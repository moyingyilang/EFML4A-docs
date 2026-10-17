# 工具链

纯 C11 自研工具，**无第三方依赖**（除 zlib）。全部源码在本目录，`make` 一键构建。

> ⚠️ 仅用于**自己拥有**的游戏文件与研究用途。不修改数值/存档，不用于竞技作弊，
> 不散布游戏资源。见仓库根目录的 [SECURITY.md](../SECURITY.md)。

---

## 1. 依赖

| 依赖 | 用途 | Termux | Debian / Ubuntu |
| --- | --- | --- | --- |
| C11 编译器 | 构建 | `pkg install clang` | `apt install build-essential` |
| `make` | 构建 | `pkg install make` | 同上 |
| zlib | `vfsfull` 的 CRC32、PNG 解码 | `pkg install zlib` | `apt install zlib1g-dev` |

已在 **aarch64 + Termux + clang** 上验证；x86_64 / glibc 同样可用。
`_GNU_SOURCE`、`pread`、`pthread` 属于 POSIX，Windows 原生 MSVC 不支持。

---

## 2. 构建

```bash
cd tools
make            # 构建全部工具到 ./build
make list       # 列出已构建的产物
make help       # 打印构建说明
make clean      # 删除 build/
```

### 只构建一个工具

```bash
make build/abpack
make build/vfsfull
```

### 改输出目录 / 编译器 / 优化级别

```bash
make BUILD=out                 # 输出到 ./out
make CC=gcc                    # 用 gcc
make CFLAGS="-O2 -g -fsanitize=address"   # 开 ASan 调试
```

### 交叉编译

```bash
make CC=aarch64-linux-gnu-gcc BUILD=build-arm64
```

### 产物

```
build/
├── vfsfull blcdump chunknames findchunk blcsetdata blcfix fextract   # .blc 层
├── abtool abpack abverify                                            # .ab 容器层
├── lz4invtest lz4invcomp4                                            # 压缩
├── pxsample scanmagenta colordetect checkrb redbox mktga mkpattern   # 图像校验
└── ms                                                                # 内存搜索
```

> 构建产物在 `build/`，已被 `.gitignore` 排除，不会进仓库。

---

## 3. 工具用法

标 🔑 的工具需要解密密钥（见 §5）。

### 3.1 `.blc`（VFS 索引层）

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| 🔑 `vfsfull` | `vfsfull <in.blc>` | 全解析：头部、chunk 表、文件条目 |
| 🔑 `blcdump` | `blcdump <in.blc>` | 解密体逐字段 dump |
| 🔑 `chunknames` | `chunknames <in.blc>` | 列出 chunk 名表 |
| 🔑 `findchunk` | `findchunk <in.blc> <global_offset>` | 全局偏移 → chunk / 块内偏移 |
| 🔑 `blcsetdata` | `blcsetdata <in.blc> <out.blc> <filename> <new_md5_hex32>` | 更新文件条目的 `dataMd5` |
| 🔑 `blcfix` | `blcfix <in.blc> <out.blc> <new_chk_md5_hex32> [md5Name_hex32]` | 更新 chunk 的 `contentMD5` + 尾部 CRC32 |
| `fextract` | `fextract <src> <listfile> <outdir> [threads]` | 多线程按 `off len name` 列表提取 |

**示例**

```bash
export VFS_KEY=<64 位十六进制>

# 结构总览（头部 + chunk 数 + 前若干文件条目）
./build/vfsfull game.blc | head -40

# 某个全局偏移落在哪个 chunk、块内偏移是多少
./build/findchunk game.blc 402297291

# 按列表批量提取（listfile 每行：偏移 长度 文件名）
printf '206810161 2438868 b9f6b1e125af334294db948d.ab\n' > list.txt
./build/fextract game.chk list.txt out/ 8
```

`fextract` 用 `pread` 并行读取，16 线程下 418 MB 约 3.5 秒。

### 3.2 `.ab` 容器层

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| `abtool` | `abtool <in.ab> [--dump]` | 校验 48 字节头，解析块表与节点表；`--dump` 额外转储数据区 |
| `abpack` | `abpack <in.ab> <out.ab> <streamOffset> <newdata.bin> [min\|pad]` | **重建容器**：在流偏移处换成新数据后重新压缩、重新混淆 |
| `abverify` | `abverify <in.ab> [out.stream] [target] [expect.bin]` | 读回校验：解出流、比对目标区 |

**`abtool` 能看出什么**

```
$ ./build/abtool bundle.ab
  .ab: 2438868 字节
  ★ 头校验通过
  compBI=326 uncompBI=567
  blocksCount=44
  ...
```

头校验公式、块表的混合字节序、节点表混淆都在
[`docs/04-ab-container.md`](../docs/04-ab-container.md) 里逐字段写明。

**`abpack` 的两种模式**

| 模式 | 行为 | 长度 | 用途 |
| --- | --- | --- | --- |
| 默认 | 重压**全部**块 | 可能变化 | 需要重新排布时 |
| `min` | 只重压与目标区重叠的块，其余**原样复制** | 可能变化 | 排除压缩器差异，定位问题 |
| `pad` | 在 `min` 基础上给未压缩块信息补填充，使压缩后长度**恰好等于原值** | **不变** | 原地等长替换 |

```bash
# 在流偏移 3869352 处替换 65536 字节，且保证 .ab 总长不变
./build/abpack in.ab out.ab 3869352 new.astc pad

# 校验：重建后流的目标区必须与新数据逐字节一致
./build/abverify out.ab out.stream 3869352 new.astc
```

**为什么 `pad` 能保持长度**

LZ4 流没有结束标记，压缩流补 `0x00` 会被当成非法 token（offset=0）。
所以不补压缩流，而是给**未压缩的块信息**追加填充（解析器忽略节点表之后的字节），
迭代搜索填充量，使压缩后长度恰好等于原 `compBI`。`uncompBI` 写填充后的大小。
详见 [`docs/05-vfs-write-path.md`](../docs/05-vfs-write-path.md)。

### 3.3 压缩

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| `lz4invtest` | `lz4invtest <in.bin>` | 压缩 → 解压 → 逐字节比对自测 |
| `lz4invcomp4` | `lz4invcomp4 <in> <out>` | LZ4Inv 压缩器（v4，与游戏压缩率差约 1.2%） |

```bash
./build/lz4invtest payload.bin      # 打印压缩率与是否 0 字节差异
./build/lz4invcomp4 in.bin out.lz4
```

### 3.4 图像校验（针对游戏截图 PNG）

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| `pxsample` | `pxsample <in.png>` | 打印尺寸/位深/颜色类型，并采样若干坐标的 RGB |
| `scanmagenta` | `scanmagenta <in.png>` | 统计品红像素（R>180, G<80, B>180） |
| `colordetect` | `colordetect <in.png>` | 检测 5 种测试纯色 + 主要饱和色 |
| `checkrb` | `checkrb <in.png>` | 检测纯红/纯蓝像素（棋盘格图案） |
| `redbox` | `redbox <in.png>` | 检测红色方框标记 |
| `mktga` | `mktga <out.tga> <w> <h> <r> <g> <b>` | 生成纯色 TGA（颜色为十六进制） |
| `mkpattern` | `mkpattern <out.tga> <w> <h>` | 生成棋盘/条纹测试图案 |

这些工具替代了原先的 Python 图像脚本：**零解释器依赖**，适合在设备上直接跑。

```bash
# 生成 256×256 纯品红
./build/mktga magenta.tga 256 256 FF 00 FF

# 验证屏幕上是否真的出现了品红
./build/scanmagenta screenshot.png
```

> 只看这几行判断是否「命中」：`scanmagenta` 输出命中数与占比，
> 占比明显大于 0 就说明替换生效了。

### 3.5 内存

| 工具 | 用法 | 作用 |
| --- | --- | --- |
| `ms` | `ms <pid> <hexpat> [max_hits] [threads]` | 多线程搜索进程内存中的字节模式 |

```bash
# 在 pid 12345 里找 4 字节模式 DE AD BE EF，最多 32 处，8 线程
./build/ms 12345 DEADBEEF 32 8
```

需要对该进程有 `ptrace` / 读 `/proc/<pid>/mem` 的权限（通常要 root）。

---

## 4. 端到端流程

以「替换某个 asset 里的一块纹理数据」为例：

```bash
export VFS_KEY=<64 位十六进制>

# ① 看索引：确认 codeVersion、chunk 数、目标文件条目
./build/vfsfull game.blc | head -40

# ② 全局偏移 → 所属 chunk 与块内偏移
./build/findchunk game.blc 402297291

# ③ 把目标 .ab 从 .chk 里按偏移抠出来
printf '206810161 2438868 target.ab\n' > list.txt
./build/fextract group.chk list.txt work/ 8

# ④ 解析容器，拿到 dataStart / 块表 / 目标在流中的偏移
./build/abtool work/target.ab --dump

# ⑤ 替换并重建（pad = 总长不变）
./build/abpack work/target.ab work/out.ab 3869352 new.astc pad

# ⑥ 读回校验
./build/abverify work/out.ab work/stream.bin 3869352 new.astc

# ⑦ 原地写回 .chk（同偏移同长度）
dd if=work/out.ab of=group.chk bs=1 seek=206810161 conv=notrunc

# ⑧ 更新两级 MD5 + CRC32
CHK_MD5=$(md5sum group.chk | cut -d' ' -f1)
./build/blcfix game.blc s1.blc "$CHK_MD5" <chunkName>
AB_MD5=$(md5sum work/out.ab | cut -d' ' -f1)
./build/blcsetdata s1.blc s2.blc "Data/Bundles/Android/initial/target.ab" "$AB_MD5"

# ⑨ 截图验证
./build/scanmagenta screenshot.png
```

每一步失败都会给出明确原因（头校验、`compBI` 越界、块信息解压失败、
`blocksCount` 不合理、目标区不匹配），不会静默产出坏文件。

---

## 5. 解密密钥

`.blc` 的上层是 ChaCha20 加密，密钥属于**游戏运行时材料**：

- 本仓库**不提供**、也不分发该密钥；
- `vfsfull` / `blcdump` / `chunknames` / `findchunk` / `blcsetdata` / `blcfix`
  从环境变量 `VFS_KEY`（**64 位十六进制**）读取；
- 未设置或格式非法时立即报错退出，不会用错误密钥静默解出垃圾数据。

```bash
export VFS_KEY=00112233...（共 64 个十六进制字符）
./build/vfsfull game.blc
```

```text
$ ./build/vfsfull game.blc
缺少环境变量 VFS_KEY（64 位十六进制）
```

**所以本目录提供的是格式与算法的完整实现，而不是开箱即用的解密器。**

`abtool` / `abpack` / `abverify` 不需要该密钥：`.ab` 层用的是 VFSAES（对合）与 LZ4Inv，
两者都在源码内（`vfsaes.c` / `deps.c`）。

---

## 6. 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `clang: command not found` | 没装编译器 | `pkg install clang`（Termux） |
| `undefined symbol: crc32` | 缺 zlib | 装 zlib；Makefile 里 `vfsfull` 已带 `-lz` |
| `缺少环境变量 VFS_KEY` | 未导出密钥 | `export VFS_KEY=...`（64 位十六进制） |
| `VFS_KEY 含非法字符` | 长度对但有非十六进制字符 | 检查是否混入 `0x` 前缀或空格 |
| `abpack` 报目标区不匹配 | `streamOffset` 算错 | 用 `abtool --dump` 确认目标在**流**中的偏移，不是文件偏移 |
| `abverify` 报 `compBI 越界` | 输入不是标准 `.ab`，或头已损坏 | 先用 `abtool` 跑一遍头校验 |
| 提取大文件很慢 / 内存高 | 线程数与单文件大小 | 调 `fextract` 的线程数；单文件是流式 `pread`，不整块读入 |
| PNG 工具报「非 PNG」 | 截图格式不是 PNG | 先转成 PNG（TGA 用 `mktga` 生成） |

---

## 7. 代码约定

- C11，除 `vfsfull` 的解析缓冲外无全局可变状态。
- 单一职责，每个工具都有 `用法` 提示，错误走 `stderr`。
- 不引入 Python / 解释器依赖：像素与图案校验同样由 C 完成。
