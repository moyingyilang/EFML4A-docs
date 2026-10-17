# 40. Unity 压缩 Mesh 格式（`.ab` 顶点数据的真相）

> 📄 **公开节选版**：完整研究记录（含逐轮实验日志、设备数据）不公开，个别历史链接已失效。

> 本文把 Unity `Mesh` 的**压缩顶点**格式写清楚，
> 对应 `docs/28` §28.35 的发现（游戏顶点是压缩的）。

## 40.1 字段对照：我们的 TypeTree vs Unity 标准

**条目 `e2cd202a…ab` 的 TypeTree 字段名（`docs/28` §28.29）：**
```
+2461   F\i)es            ← m_Faces
+2474   normalNtang       ← m_Normals / m_Tangents
+2520   channel           ← m_Channels
+2561   GfullWeights      ← m_FullWeights
+2577   indPose           ← m_BindPose
+2676   MinMax            ← ★
+2694   Variable          ← ★
+2720   #Compressio       ← m_Compression
+2744   m_KeepV           ← m_KeepVertices
+2789   Convex            ←
+2809   Buffer            ← m_VertexBuffer
+2843   streaz            ← m_StreamData
+2850   offset            ← m_Offset
+2861   dim3n             ← m_Dimension
+2870   KQSize            ← m_Size
+2889   PackedB…tVector   ← ★ m_PackedBitVector
+2897   tVector           ←
+2969   /FloatC           ← m_FloatColors
+2986   %Trian&les        ← m_Triangles
```

**⇒ `m_Compression` / `m_PackedBitVector` / `MinMax` / `m_KeepVertices` /
`m_FloatColors` / `m_Triangles` 全部对应 **Unity 的 `CompressedMesh`**。**

## 40.2 Unity 标准结构（来自 AssetStudio / UnityPy 的公开实现）

### `CompressedMesh`

```csharp
class CompressedMesh {
    PackedFloatVector m_Vertices;      // 顶点位置
    PackedFloatVector m_UV;            // UV
    PackedFloatVector m_Normals;       // 法线
    PackedFloatVector m_Tangents;      // 切线
    PackedIntVector   m_Weights;       // 骨骼权重
    PackedIntVector   m_NormalSigns;   // 法线符号
    PackedIntVector   m_TangentSigns;  // 切线符号
    PackedFloatVector m_FloatColors;   // 顶点色
    PackedIntVector   m_BoneIndices;   // 骨骼索引
    PackedIntVector   m_Triangles;     // 三角形索引
}
```

### `PackedFloatVector`（关键）

```csharp
class PackedFloatVector {
    uint  m_NumItems;   // 元素个数
    float m_Range;      // = 1.0 / (max - min)   量化范围
    float m_Start;      // = min                  量化起点
    int   m_DataSize;   // ★ Unity Array：u32 长度（不是 u8！）
    byte[] m_Data;      // 量化后的定长位流
}
```

**⚠️ 踩坑记录**：我最初把 `m_DataSize` 当成 `u8`，**搜不到任何东西**。
**实际是 `int`（Unity `Array` 的 `u32` 长度前缀）**，修正后立刻命中 269 处。

### `PackedIntVector`

```csharp
class PackedIntVector {
    uint  m_NumItems;
    int   m_DataSize;   // ★ 同样是 u32
    byte[] m_Data;      // 位流
}
```

## 40.3 ★ 量化原理（`PackedFloatVector`）

**存储时：**
```
quantized = round((value - m_Start) * m_Range)
```
**其中 `m_Range = 1.0 / (max - min)`，量化位数由 `m_DataSize` 与 `m_NumItems` 决定：**
```
bitsPerItem = (m_DataSize * 8) / m_NumItems        // 通常 8/16/32
```

**读取时：**
```
value = (quantized / ((1 << bitsPerItem) - 1)) / m_Range + m_Start
```

**⇒ 这就是「顶点不是 `float32`」的原因**：坐标被量化成了 8/16 位的整数位流。****

## 40.4 ★ 对我们的意义（修正重打包方案）

**当前错误做法：**
```
把 EFMI 的 VB0（3×float32 位置，未压缩）直接写进游戏的顶点区
⇒ 游戏按【量化的位流】解读 ⇒ 坐标完全错乱 ⇒ 模型扭曲
```

**正确做法（两条路）：**

### 路 A：按游戏参数量化 EFMI 顶点（推荐）

```
1. 读出游戏的 m_Vertices 的 { m_NumItems, m_Range, m_Start, m_DataSize }
2. 把 EFMI 的顶点位置按同一公式量化:
     q = round((v - m_Start) * m_Range)
   再按 bitsPerItem 打包成位流
3. 用打包后的位流替换 m_Data
   ⇒ 数据长度不变（等长替换仍然成立！）
```

### 路 B：改 `m_Compression` 为 None（不推荐）

```
把 m_Compression 置 0 并写未压缩顶点
⚠ 会改变 m_DataSize / 数据长度 ⇒ 破坏等长替换前提
```

## 40.5 ★ 下一步（可执行）

| 步骤 | 做法 |
| --- | --- |
| **1** | **在条目里定位 `m_Vertices` 的 `PackedFloatVector` 头**（4 个字段：`u32 numItems` + `f32 range` + `f32 start` + `u8 dataSize`） |
| **2** | **验证**：`numItems` 应是顶点数；`range` 与 `start` 应能推出合理的坐标范围 |
| **3** | **解码位流** ⇒ 得到真实顶点坐标 ⇒ 与 `m_Center/m_Extent` 对比验证 |
| **4** | **用同一参数量化 EFMI 顶点** ⇒ 等长替换 |

**判据（第 1 步的搜索）**：
```
numItems ∈ [1000, 200000]
range    ∈ (0, 1e6]      （= 1/(max-min)，通常几百到几千）
start    ∈ [-100, 100]
dataSize = ceil(numItems * bitsPerItem / 8)，bits ∈ {8,16,32}
         ⇒ dataSize / numItems ≈ 1 / 2 / 4
```

## 40.6 工具

**见 `tools/vfs/meshdec.c`** —— 扫描条目、定位 `PackedFloatVector`、解码并导出顶点。


---

## 40.7 实测：条目内搜 `PackedFloatVector`（第 52 轮）

**判据：**
```
numItems ∈ [100, 2e6]
range    ∈ (1e-5, 1e8)
start    ∈ (-10000, 10000)
dataSize = u32，且 (dataSize*8/numItems) ∈ (0.9, 33)
dataSize ≤ 条目剩余长度
```

**结果：命中 269 处。其中 `range` 与我们网格尺度吻合的候选：**

| 偏移 | `numItems` | `range` | `start` | `dataSize` | bits/item | 判读 |
| --- | --- | --- | --- | --- | --- | --- |
| **+111264** | 81,266 | **1.002262** | 0 | 265,473 | 26.13 | 跨度 0.998 |
| **+229958** | 16,384 | **2.000004** | 0 | 10,240 | 5.00 | **跨度 0.5** |
| **+236583** | 4,096 | **2.054699** | 0 | 4,096 | 8.00 | **跨度 0.487** |
| **+233647** | 1,048,704 | 4.224609 | 0 | 1,048,704 | 8.00 | 跨度 0.237 |

**我们的网格 `m_Extent = (0.0455, 0.1829, 0.0445)`：**
```
最大跨度 = 2 × 0.1829 = 0.366   ⇒  range ≈ 1/0.366 = 2.73
⇒ 【range = 2.000004（跨度 0.5）与 2.054699（跨度 0.487）尺度吻合】
```

**⇒ 候选指向 `+229958` / `+236583` 附近。**

### ⚠️ 但仍需收紧判据

**269 处命中里大量是**假阳性**（例如 `range=133632` / `start=0` / `dataSize==numItems`
⇒ 跨度 7.5e-6，不可能是顶点坐标）。**

**⇒ 需要更强的验证：**
```
① numItems 应与 m_IndexCount/m_VertexCount 相关
② range × (max-min) = 1，且 max-min 应与 m_Extent 一致
③ 解码位流后，坐标包围盒应≈ m_Center ± m_Extent
```

**⇒ 这三条的 ③ 是最强判据：**解码后必须能重现 `m_LocalAABB`**。**

---

## 40.8 验证进展与卡点（第 54 轮）

### 40.8.1 `m_StreamData` 检查结果

**在条目里搜 `StreamingInfo` 的典型 path 形式：**
```
archive:/   → 未找到
.resS       → 未找到
.resource   → 未找到
CAB-        → 未找到
.assets     → 未找到
Assets/     → 未找到
```

**⇒ **条目内没有外置资源路径** ⇒ `m_StreamData` 大概为空
⇒ **顶点数据应该就在这个条目内**（不是外置的）。**

### 40.8.2 ★ 放宽 stride 判据后仍然失败

**之前要求 `-10 < v < 10` 且 `|v| > 1e-7`；
现在**去掉 `|v| > 1e-7`**（角色网格很多顶点坐标恰好为 0，会被误杀）：**

```
stride= 8: 38.0%    stride=12: 38.9%    stride=16: 38.0%
stride=20: 43.3%    stride=24: 48.2%    stride=28: 50.1%
stride=32: 49.1%    stride=36: 45.9%    stride=40: 42.8%
stride=48: 39.0%    stride=56: 37.8%    stride=64: 36.5%
```

**⇒ 仍无 stride > 50%。**
**⇒ **`+12,800` 区域不是 strided `float32` 顶点流**（与 §28.37 一致）。**

### 40.8.3 ★ `meshdec` 的决定性测试失败

**用 `meshdec try` 搜「解码后能重现 `m_LocalAABB`」的候选：**
```
目标: m_Center=(-0.1942, 0.0033, -0.0368)  m_Extent=(0.0455, 0.1829, 0.0445)
结果: 无 err<0.02 的候选
最佳: @+233105  numItems=1048704  range=16.906  bits=8
      解码包围盒 (0,0,0)..(0.0591,0.0591,0.0591)  err=0.50
```

**已修两个 bug（都记录下来，避免重复踩）：**
```
① m_DataSize 是 int（Unity Array 的 u32 长度），不是 u8
② bitsPerItem 用【整数除法】: bits = (dataSize*8) / numItems
```

### 40.8.4 ★ 三种可能（未定）

| 可能 | 说明 | 验证方式 |
| --- | --- | --- |
| **A. 顶点在条目内但非 PackedFloatVector** | 也许 `m_Compression = 0`（不压缩），顶点在 `m_VertexBuffer` 里，但布局与我假设的不同 | 定位 `m_VertexData.m_Channels` |
| **B. 顶点在别的地方** | `m_StreamData` 为空但数据仍在别的 `.chk`/块里 | 全局搜索同尺寸的 float 密集区 |
| **C. 魔改格式** | 鹰角改过 Unity（`HGShader` 已证明），Mesh 序列化可能也改了 | 反汇编 `libunity.so` 的 Mesh 读写 |

### 40.8.5 ★ 下一步的最强突破口

**`m_VertexData` 的 `m_Channels` 描述顶点布局**（见 `docs/28` §28.29）：

```c
struct ChannelInfo { UInt8 stream; UInt8 offset; UInt8 format; UInt8 dimension; };
m_VertexData {
    u32 m_VertexCount;
    ChannelInfo m_Channels[14];   // 56 B
    u32 m_DataSize;
    byte _typelessdata[m_DataSize];
}
```

**⇒ 若 `_typelessdata` 从 `+12,800` 开始，则：**
```
m_Channels  @ +12,800 - 4 - 56 = +12,740
m_VertexCount @ +12,736
m_DataSize  @ +12,796
```

**⇒ **下一步：精确检查 `+12,736..+12,800` 这 64 字节** —— 若其中能读出
`m_VertexCount`（合理顶点数）与 `m_Channels`（stream/offset/format/dimension），
就能**直接得到顶点布局**，不需要解码位流。**

**⚠️ 之前粗看过 `+12,736` 得到的是「垃圾值」，但那可能是对齐偏移错了（差 0/4/8 字节）。**
**⇒ 值得用「`m_Channels` 合法性」做判据系统地扫一遍。**

---

## 40.9 新增工具 `tools/vfs/meshdec.c`

```bash
meshdec scan  <file> [起] [止]               # 扫描并列出 PackedFloatVector 候选
meshdec try   <file> <cx> <cy> <cz> <ex> <ey> <ez>
                                             # 自动找「解码后能重现该包围盒」的候选
```

**要点（易踩坑）：**
```
· m_DataSize 是 int（u32），不是 u8
· bitsPerItem = (dataSize * 8) / numItems   ← 整数除法
· 解量化: value = (q / ((1<<bits)-1)) / range + start
· 位流按小端逐位读取
```

---

## 40.10 ★★★★★ 重大修正：`+12,800` **不是**顶点流（第 55 轮）

### 40.10.1 决定性测量

**从 `+12,800` 起，测「连续多少个 16 字节块满足 `第 0/4/8 字节都是合理 float32`」：**

```
判据 [-10, 10]   ⇒ 连续 17 个块 = 272 字节
判据 [-100, 100] ⇒ 连续 17 个块 = 272 字节
```

**换成 stride=12（纯位置流）：**
```
⇒ 连续 23 个块 = 276 字节
```

**全条目扫「≥100 连续块」的段：**
```
仅 1 个：+2,377,136（114 块 = 1,824 字节）
```

### 40.10.2 ★★ 结论

**⇒ 条目内**几乎没有密集 `float32` 段**。**
**⇒ `+12,800` 处只有**前 272 字节**是 `float` 数据，之后就断了。**
**⇒ 这**不是** 7,911 个顶点的流（那应该是 126,576 字节）。**

### 40.10.3 ★ 这解释了「模型变了但扭曲」的真正原因

```
我们写入 126,576 字节（7,911 顶点 × 16 B）
但 +12,800 处只有【前 272 字节】是 float 数据
⇒ 其余 126,304 字节【覆盖了别的东西】
⇒ 模型「变了」（破坏生效）但「扭曲」（大面积损坏）
```

**⇒ **我们一直在改错地方。****
**⇒ 那 272 字节的 `float` 段更像是某个 `Vector3` 数组
（包围盒 / 矩阵 / UV 表），不是顶点流。**

### 40.10.4 修正后的判断

| 此前的判断 | 修正 |
| --- | --- |
| `+12,800` = 顶点区（stride 16） | ❌ **只有 272 字节是 float，不是顶点流** |
| 顶点「压缩了」所以找不到 float | 🟡 **更可能：顶点根本不在这个条目里** |
| 模型变化 = mod 生效 | ⚠️ **模型变化 = 破坏了 272 字节之后的某字段** |

### 40.10.5 ⏭️ 新的搜索方向

**这个条目的区段：**
```
+0            .. +2,330      外层容器头
+2,330        .. +5,571      TypeTree
+5,571        .. +8,264      Mesh 对象字段
+8,264        .. +11,700     m_BindPose 矩阵（含 ±1.0 单位矩阵）
+11,700       .. +12,776     其他
+12,776       .. +12,800     m_Center / m_Extent
+12,800       .. +12,800+272 ★ 一个小 float 数组（不是顶点流）
...
+288,693      骨骼名表（61 个名字）
+288,693      .. +2,399,399  ★★【约 2.1 MB 未查过】
```

**⇒ **顶点流最可能在骨骼表之后**（Unity 常把大数组放在对象末尾，
且 `m_BindPose` 用的矩阵区在 +8,264 已经排除了）。**

**⇒ 下一步：用同一判据扫 `+288,693` 之后。**

---

## 40.11 ★★★★★ 全量扫描结论：InitBundle 里**没有密集 float32 顶点流**（第 56 轮）

### 40.11.1 新工具 `tools/vfs/floatscan.c`

```bash
floatscan <file> [stride] [minBlocks] [lo] [hi]
# 默认 stride=16   minBlocks=200   range=[-100,100]
# ⇒ 列出所有「≥minBlocks 个连续 stride 块，每块第 0/4/8 字节都是合理 float32」的段
```

**⚠️ 关键：必须排除 denormal / ≈0 值**（否则递增整数序列会被误判）：
```c
if(fabsf(x)<1e-6f && fabsf(y)<1e-6f && fabsf(z)<1e-6f) break;
```

### 40.11.2 ★ 踩坑：一次假阳性

**修正前，工具报出：**
```
+26,807,648   6,924 块 = 110,784 字节   ← 看起来正是 7k 个顶点！
```
**实际内容是：**
```
09 34 00 00 | 0a 34 00 00 | 0b 34 00 00 | 0c 34 00 00 ...
⇒ 递增整数序列 0x340009, 0x34000a, 0x34000b, ...
⇒ 作为 float32 是 denormal（1.8e-41 ≈ 0.0）
⇒ 被 -100 < v < 100 的判据误收
```

**⇒ 教训：`float` 判据必须同时排除「接近 0」与「超出范围」。**

### 40.11.3 修正后的全量扫描结果

**`InitBundle` 的 `.chk`（217,713,773 字节，1052 个条目）：**

| stride | 最长的密集 float 段 |
| --- | --- |
| **16** | 848 块（13,568 字节） |
| **12** | 1,131 块（13,572 字节） |
| **32** | 424 块（13,568 字节） |

**⇒ 全部只有 ~840–1,130 块。**
**⇒ 远不及一个顶点流（7,911 个顶点需要 7,911 块）。**

### 40.11.4 ★★ 最终结论

**⇒ **游戏网格的顶点数据**不是**以密集 `float32` 存储的。****
**⇒ **顶点确实是量化/压缩的**（Unity `CompressedMesh`）——
与 §28.35 / §40.2 的判断一致。**

**⇒ 因此：**
```
❌ 不可能「直接写 3×float32 顶点」
✅ 必须【按游戏的量化参数重新压缩】顶点
✅ 或者【改 m_Compression=0 走未压缩】（但会破坏等长替换）
```

### 40.11.5 ⏭️ 下一步（聚焦）

| 优先 | 任务 | 判据 |
| --- | --- | --- |
| **1** | **用 `meshdec try` 时把搜索范围扩到整个 `.chk`**（不只条目内） | 解码后重现 `m_LocalAABB` |
| **2** | **修 `meshdec` 的位流解码**（可能是 LSB-first vs MSB-first，或 `numItems` 含义） | 同上 |
| **3** | **确认 `m_VertexCount`**：从 `m_IndexCount` 反推（索引最大值 + 1 = 顶点数） | 需要一个索引缓冲 |
| 4 | 用现成工具（`AssetStudio` / `UnityPy`）交叉验证 | — |

**⚠️ 关键洞察：EFMI 的 Component0 尺寸是 `0.32×0.26×0.32`，
而游戏 `e2cd202a` 的 `m_Extent` 是 `0.0455×0.1829×0.0445`（尺寸 `0.09×0.37×0.09`）。
⇒ **两者不是同一个部件** ⇒ 用 Component0 去替换它是错的目标。**

---

## 40.12 ★★★★★ AssetStudio 的精确 API（第 57 轮，关键突破）

**用 .NET 反射读出了 `AssetStudio.dll` 的真实字段（Termux 上装的 .NET SDK 8.0.132）。**

### 40.12.1 ★ `PackedFloatVector`（真字段）

```csharp
public class PackedFloatVector {
    public Byte     m_BitSize;    // ★★ 位宽是【独立字段】！
    public Byte[]   m_Data;
    public UInt32   m_NumItems;
    public Single   m_Range;
    public Single   m_Start;
    public Single[] UnpackFloats(Int32,Int32,Int32,Int32);  // ★ 现成解包方法
}
```

**⇒ **修正**：我之前猜「`bits = dataSize*8/numItems`（整数除法）」——
方向不对。**位宽是显式存储的 `m_BitSize` 字段。****

### 40.12.2 ★★ `VertexData`（关键）

```csharp
public class VertexData {
    public ChannelInfo[] m_Channels;
    public UInt32        m_CurrentChannels;
    public Byte[]        m_DataSize;      // ★ Unity Array（int 长度 + 数据）
    public StreamInfo[]  m_Streams;       // ★★ 独立 Stream！
    public UInt32        m_VertexCount;
}
```

**⇒ `m_Streams` 的存在**证明顶点数据是**分通道流**存储的**！
**⇒ 这正好解释了为什么「按固定 stride 扫」一直失败 ——
各通道在**独立的 stream** 里，不是交错在一个 stride 里。**

### 40.12.3 `CompressedMesh` 完整字段

```csharp
public class CompressedMesh {
    public PackedFloatVector m_Vertices;      // 位置
    public PackedFloatVector m_Normals;       // 法线
    public PackedFloatVector m_Tangents;      // 切线
    public PackedFloatVector m_UV;            // UV
    public PackedFloatVector m_FloatColors;   // 顶点色
    public PackedFloatVector m_BindPoses;     // ★ 绑定姿态（就是我们在 +8264 看到的矩阵！）
    public PackedIntVector   m_Weights;
    public PackedIntVector   m_NormalSigns;
    public PackedIntVector   m_TangentSigns;
    public PackedIntVector   m_BoneIndices;
    public PackedIntVector   m_Triangles;
    public PackedIntVector   m_Colors;
    public UInt32            m_UVInfo;
}
```

### 40.12.4 `Mesh` 类的字段（AssetStudio 已解压）

```csharp
public class Mesh : NamedObject {
    public UInt32        m_VertexCount;
    public Single[]      m_Vertices;      // ★ 解压后的位置！
    public Single[]      m_Normals;
    public Single[]      m_Tangents;
    public Single[]      m_UV0 … m_UV7;
    public Single[]      m_Colors;
    public List<uint>    m_Indices;
    public SubMesh[]     m_SubMeshes;
    public BoneWeights4[] m_Skin;
    public Matrix4x4[]   m_BindPose;
    public UInt32[]      m_BoneNameHashes;
    public BlendShapeData m_Shapes;
}
```

**⇒ **如果 AssetStudio 能读这个 `.ab`，它就直接把解压后的 `m_Vertices` 给我了！****

### 40.12.5 ★★ 两条路（都可行）

| # | 路 | 说明 |
| --- | --- | --- |
| **①** | **让 AssetStudio 真读 `.ab`** | 若读到 `Mesh`，直接 dump `m_Vertices` / `m_VertexCount` / `m_SubMeshes` |
| **②** | **只借用它的解包算法** | 用反射调 `PackedFloatVector.UnpackFloats`，喂我自己定位的候选<br>**不需要它解析容器** ⇒ 绕过魔改格式的问题 |

**⇒ 路 ② 特别有价值：**只要我能定位 `PackedFloatVector` 的头，
就能用 Unity 官方的解包逻辑拿到顶点，不用自己猜位流。****

### 40.12.6 环境（已就绪）

```
.NET SDK : 8.0.132   (linux-bionic-arm64)
AssetStudio.dll     : 518,656 B（已解压到 ~/.efml4a/scratch/as/x/）
反射验证 : ✓ Mesh / CompressedMesh / PackedFloatVector / VertexData 全部可读
```

### 40.12.7 ⚠️ 分工（按用户要求）

```
研究/验证  → 可用 .NET / C# / Python（现成工具）
自有落地工具 → 一律 C/C++
```

---

## 40.13 ★★★★★ 决定性突破：`StreamInfo` —— 每个 stream 有独立 stride（第 58 轮）

### 40.13.1 反射拿到的精确签名

**`PackedFloatVector`：**
```csharp
public Single[] UnpackFloats(Int32 itemCountInChunk, Int32 chunkStride,
                             Int32 start, Int32 numChunks);
```
**⇒ 压缩 Mesh 的解包是**分 chunk**的（不是连续位流）—— 我此前的假设错了。**

**`PackedIntVector`：**
```csharp
public Int32[] UnpackInts();
```

**`VertexData`：**
```csharp
void GetStreams(Int32[] version);
void GetChannels(Int32[] version);
```

### 40.13.2 ★★ `StreamInfo` —— 核心答案

```csharp
public class StreamInfo {
    public UInt32 channelMask;
    public UInt32 offset;       // 顶点数据在该 stream 内的偏移
    public UInt32 stride;       // ★★ 每个 stream 独立的 stride！
    public UInt32 align;
    public Byte   dividerOp;
    public UInt16 frequency;
}
```

### 40.13.3 ★ `ChannelInfo`

```csharp
public class ChannelInfo {
    public Byte stream;      // ★ 该通道属于哪个 stream
    public Byte offset;      // ★ 在该 stream 内的字节偏移
    public Byte format;      // 数据类型（float32 / half / normal…）
    public Byte dimension;   // 维度 1–4
}
```

### 40.13.4 ★★★ 这完美解释了此前的所有失败

```
我一直在做「按单一 stride 扫全区域」：

  +12,800 起，测 stride ∈ {4,8,12,…,80}
  ⇒ 全部 <50% ⇒ 判定「不是 float 顶点流」

但真实结构是：
  位置 → stream 0（stride A）
  法线 → stream 1（stride B）
  UV   → stream 2（stride C）
⇒ 【各通道在不同的 stream 里，stride 各不相同】
⇒ 用单一 stride 扫【永远扫不出周期性】
```

**⇒ **这就是「差一步」的真正结构性原因。****
**⇒ 顶点数据**不是**「一个交错的大数组」，而是**多个独立的 stream**。**

### 40.13.5 ★ 下一步（清晰了）

| 步骤 | 做法 |
| --- | --- |
| **1** | **定位 `m_VertexData` 的起点**（`m_VertexCount` + `m_Channels[14]`） |
| **2** | **读出 `m_Streams` 数组** ⇒ 得到每个 stream 的 `{offset, stride}` |
| **3** | **按 stream 分别扫描** ⇒ 用**每个 stream 自己的 stride** 找顶点数据 |
| **4** | **按 `m_Channels` 组装顶点** ⇒ 位置/法线/UV 各取对应 stream 的对应偏移 |
| **5** | **写 C 工具**（按用户要求）：读取布局 + 替换 EFMI 顶点 |

**⚠️ 注意**：`m_Streams` 只在 Unity 2019+ 且 `m_VertexData` 用多流时有意义。
**若 `m_Streams` 为空，则退化为单一交错结构（那就是我原来的假设）。**

### 40.13.6 环境

```
.NET SDK 8.0.132（Termux 装）
AssetStudio.net6 v0.16.47（ghfast.top 镜像）
反射：Mesh / CompressedMesh / PackedFloatVector / PackedIntVector /
      VertexData / ChannelInfo / StreamInfo 全部可读
```

---

## 40.14 ★★★ AssetStudio 的 `Mesh` 解压方法链（第 59 轮）

### 40.14.1 反射拿到的 `Mesh` 方法

```csharp
void  ReadVertexData();              // ★ 读顶点数据（含 VertexData）
void  DecompressCompressedMesh();    // ★★ 解压压缩网格（核心！）
void  ProcessData();
void  GetTriangles();
void  InitMSkin();
void  SetUV(Int32 uv, Single[] m_UV);
Single[] GetUV(Int32 uv);
```

### 40.14.2 `VertexData` 的构造

```csharp
VertexData(ObjectReader reader);     // ← 需要 ObjectReader 上下文
void GetStreams(Int32[] version);
void GetChannels(Int32[] version);
```

### 40.14.3 ★ 计划：**借用它的解压逻辑，绕过容器解析**

**思路：AssetStudio 读不了我们的魔改容器，但它的**解压算法**是标准的。
可以只借算法：**

```
① 构造一个 Mesh 对象
② 用反射把它的 reader 指向我的字节流（或伪造最小 SerializedFile 上下文）
③ 直接调 ReadVertexData() + DecompressCompressedMesh()
④ 读回 m_Vertices / m_VertexCount / m_Indices / m_SubMeshes
```

**⚠️ 前置依赖**：`VertexData` 需要 `ObjectReader`，
而 `ObjectReader` 需要 `SerializedFile` / `SerializedType` 等上下文
⇒ 需要**伪造一个最小的 `SerializedFile` 上下文**。

### 40.14.4 ★ 更简单的退路（推荐先试）

**只借 `PackedFloatVector.UnpackFloats`：**

```csharp
Single[] UnpackFloats(Int32 itemCountInChunk, Int32 chunkStride,
                      Int32 start, Int32 numChunks);
```

**思路：**
```
1) 我自己在 C 里定位 PackedFloatVector 的头（numItems/range/start/bitSize/data）
2) 在 C# 里【手工构造】一个 PackedFloatVector 对象（反射设字段）
3) 调它的 UnpackFloats ⇒ 拿到 float[]
4) 验证解码结果能否重现 m_LocalAABB
```

**⇒ 优点：**完全不需要 SerializedFile 上下文，只需要 5 个字段 + 一次方法调用。****

### 40.14.5 环境

```
.NET SDK 8.0.132（Termux）
AssetStudio.net6 v0.16.47（ghfast.top）
反射已验证：Mesh / VertexData / PackedFloatVector / PackedIntVector /
            ChannelInfo / StreamInfo / EndianBinaryReader 全部可读可调
```

---

## 40.15 `PackedFloatVector` 的构造方式（第 60 轮）

### 40.15.1 反射结果

```
PackedFloatVector 的方法（全部）:
  pub  Single[] UnpackFloats(Int32 itemCountInChunk, Int32 chunkStride,
                             Int32 start, Int32 numChunks)
  pub  ToString() / Equals() / GetHashCode() / GetType() / MemberwiseClone()
  ⇒ 【没有 Read 方法】
  ⇒ 【没有无参构造函数】

字段：m_NumItems(u32) m_Range(f32) m_Start(f32) m_BitSize(u8) m_Data(byte[])
```

### 40.15.2 结论

**AssetStudio 是在 `CompressedMesh.Read` 里**内联读取** `PackedFloatVector` 的，
`PackedFloatVector` 类本身只有 `UnpackFloats` 算法。**

**⇒ 要借用它的算法，需要：**
```csharp
var pf = (PackedFloatVector)FormatterServices.GetUninitializedObject(typeof(PackedFloatVector));
// 反射设 5 个字段
f_NumItems.SetValue(pf, numItems);
f_Range.SetValue(pf, range);
f_Start.SetValue(pf, start);
f_BitSize.SetValue(pf, bitSize);
f_Data.SetValue(pf, dataBytes);
// 调算法
float[] v = pf.UnpackFloats(itemCountInChunk, chunkStride, start_, numChunks);
```

### 40.15.3 ★ 关键的未知量：`UnpackFloats` 的 4 个参数

```csharp
UnpackFloats(Int32 itemCountInChunk, Int32 chunkStride, Int32 start, Int32 numChunks)
```

**Unity 的 `MeshCompression` 用**分块打包**：**
```
每 itemCountInChunk 个元素为一组，每组的数据对齐到 chunkStride 字节
⇒ 这就是为什么【连续位流解码会失败】
⇒ 参数取值需要从调用点确定
```

**⇒ 下一步：反汇编（或用 IL 读取）`CompressedMesh.Read` / `Mesh.DecompressCompressedMesh`
里调用 `UnpackFloats` 的地方，看它传什么参数。**
**⇒ 这是唯一还没确定的关键参数。**

### 40.15.4 工具（本轮新增）

```csharp
// 40.15.5 记录：本轮的 C# 探针
// ~/.efml4a/scratch/asx/probe/Program.cs
// 功能: 反射读 AssetStudio 的类型/方法/字段，并尝试调用
```

---

## 40.16 ★★★★★ IL 解出全部 `UnpackFloats` 调用参数（第 61 轮）

**直接读 `Mesh.DecompressCompressedMesh()` 的 IL，找 `callvirt UnpackFloats` 的调用点。**

### 40.16.1 实测调用点

| IL 偏移 | 片段（关键字节） | 解出的参数 | 对应字段 |
| --- | --- | --- | --- |
| `IL_003c` | `7B 7D020004` `19` `1F 0C` `16` `15` `6F` | **`(3, 12, 0, 1)`** | **`m_Vertices`** |
| `IL_00de` | `7B 7E020004` `18` `1E` `16` … | `(2, 8, 0, <动态>)` | `m_UV` |
| `IL_017a` | `7B 7F020004` `1F 10` `1F 40` `16` `15` | `(16, 64, 0, 1)` | 法线/切线 |
| `IL_01eb` | `7B 80020004` `18` `1E` `16` `15` | `(2, 8, 0, 1)` | — |
| `IL_047f` | `7B 85020004` `17` `1A` `16` `15` | `(1, 4, 0, 1)` | `m_FloatColors` |
| `IL_01fd` 等 6 处 | `6F 7C000004` | `UnpackInts(...)` | 权重/索引/三角形 |

### 40.16.2 ★★ 参数语义（终于确定）

```csharp
UnpackFloats(Int32 itemCountInChunk,   // 每组元素数
             Int32 chunkStride,        // ★ 每组占的【字节数】
             Int32 start,              // 起始偏移
             Int32 numChunks)          // 组数
```

| 用途 | `itemCountInChunk` | `chunkStride` | 说明 |
| --- | --- | --- | --- |
| **顶点位置** | **3** | **12** | 每顶点 3×float32 = 12 字节 |
| 法线/切线 | 16 | 64 | 4×float32 ×4 |
| UV | 2 | 8 | 2×float32 = 8 字节 |
| 顶点色 | 1 | 4 | 1×float32 = 4 字节 |

### 40.16.3 ★★★ 为什么我之前一直失败

```
我假设：顶点数据是【连续位流】或【单一交错 stride】

真相：数据按【元素组分块】存储，每组有自己的字节步长
  m_Vertices : 每 3 个 float 一组，组间步长 12 字节
  m_UV       : 每 2 个 float 一组，组间步长  8 字节

⇒ 各组【独立】，不是一个大数组
⇒ 用「单一 stride 扫全区域」永远扫不出结果
⇒ 这就是「差一步」的真正结构性原因
```

### 40.16.4 ★ 但也说明一件事：`m_Vertices` 是**逐顶点 12 字节**

**⇒ 若 `UnpackFloats(3, 12, 0, n)` 就是「每 3 个 float、步长 12」，
那 `m_Vertices` 的 `m_Data` 里就是**顺序排列的 float32 坐标**
（`m_Data` 由 `m_BitSize` 决定是否位压缩）。**

**⇒ **下一步的关键**：`m_BitSize` 的实际值**
```
若 m_BitSize == 32 ⇒ m_Data 就是原始 float32 数组
若 m_BitSize < 32  ⇒ 需要按位解包
```

### 40.16.5 ⚠️ 方法学教训

```
✅ 读 IL 比猜格式快 10 倍
   （一次 IL 分析就拿到了我猜了 8 轮的参数）
✅ 有现成实现时，【看它怎么做】比【自己试】高效
```

---

## 40.17 ★ IL 深挖：字段是**算出来的**，不在文件里（第 62 轮）

### 40.17.1 决定性发现

**在 `Mesh.DecompressCompressedMesh` 的 IL 里找 `stfld PackedIntVector.m_NumItems` / `m_BitSize`：**

```
@IL_0655 stfld PackedIntVector.m_NumItems
   前序: 25(dup) 7B-F5(ldfld) 1A(ldc.i4.4) 5A(mul)
   ⇒ m_NumItems = <字段> × 4

@IL_066e stfld PackedIntVector.m_BitSize
   前序: 25(dup) 7B-F7(ldfld) 1A(ldc.i4.4) 5B(mul) D2(??)
   ⇒ m_BitSize = <字段> × 4 …（或有其他运算）
```

### 40.17.2 ★★ 结论

**⇒ `m_NumItems` / `m_BitSize` **不是从文件里读的**，
而是**从另一个字段（数据长度）推算**出来的。」
**⇒ 这解释了为什么「在文件里搜 `numItems` 字段」总是不准 —— 文件里**根本没有**它。**

**这与我 `docs/40` §40.11 的扫描失败完全吻合：
我当时用「`numItems` 是文件里的 u32」当判据，**前提就是错的**。**

### 40.17.3 `ReadVertexData` 的 IL（1022 字节）

**开头片段：**
```
IL_0000: 02 02 7B-CE020004 7B-95020004 7D-C0020004 16 0A 38-D2030000
         ldarg.0 ldarg.0 ldfld       ldfld       stfld       ldc.i4.0 stloc br
IL_0030: 02 7B-CE020004 7B-97020004 07 7B-90020004 9A 0C 17 8D4F000001 25 16 08 7B-8A020004 9E
IL_0090: … 28-E1000006(call) 0D 09 28-E2000006(call) 1304 …
```

**⇒ `ReadVertexData` 调了两个方法（token `E1` / `E2`），
很可能就是 `GetChannels(version)` 与 `GetStreams(version)`。**

### 40.17.4 `GetChannels` / `GetStreams` 的 IL（已取到）

```
GetChannels : 296 字节
  IL_0000: 02 1C 8D6A000002 7D-96020004 16 0A 2B-11 02 7B-96020004 06 73-CD000006 A2 06 17 58 0A 06 1C 32-EB
  IL_0018: 16 0B 38-ED000000 02 7B-97020004 07 9A 0C 17 8D4F000001 25 16 08 7B-8A020004 9E 73-F400000A 0D 16 1304 …
  IL_0060: … 02 7B-96020004 1105 9A 1306 1106 07 D2 7D-90020004 1106 …

GetStreams : 247 字节
  IL_0000: 02 7B-96020004 7E-05080004 25 2D-17 26 7E-04080004 FE-06 C9030006 73-F200000A 25 80-05080004 28-0B0000 2B-17 58 0A 02 06 8D-69000002 7D-97020004 16 0B 16 0C 38-AD000000 16 0D 16 1304 16 1305 2B-4E 02 7B-96020004 1105 9A 1306 1106 7B-90020004 08 33-33 1106 7B-93020004 16 31-29 09 17 1105 1F-1F 5F 62 60 0D 1104 11 …
```

### 40.17.5 ⏭️ 下一步

**手工反汇编 `GetStreams`（247 字节，不长）⇒ 得到 `m_Streams` 的精确读取顺序。**
**再从 `GetStreams` 的 IL 推出 `StreamInfo` 各字段的读取位置。**

**⚠️ 另一个重要修正（记录）**：
**§40.11 的全量扫描用了错误前提（`numItems` 在文件里），
⇒ 那个「无密集 float 顶点流」的结论**需要重新评估**（但顶点确实是压缩的这一点不变）。**

---

## 40.18 ★★★★★ 游戏 `Mesh` 类有 `HGGetPtrUnchecked()`（第 63 轮）

**来源：游戏 il2cpp dump（`dump.cs`，81,901,728 字节，来自 `~/.efml4a/scratch/qq/dump.zip`）。**

### 40.18.1 游戏 `Mesh` 类（`UnityEngine` 命名空间）

```csharp
// Dll : UnityEngine.CoreModule.dll
// Namespace: UnityEngine
public sealed class Mesh : Object
{
    // Properties
    public IndexFormat   indexFormat { get; set; }
    public Target        vertexBufferTarget { get; set; }
    public Int32         blendShapeCount { get; }
    public Matrix4x4[]   bindposes { get; set; }
    public Boolean       isReadable { get; }
    public Int32         vertexCount { get; }
    public Int32         subMeshCount { get; set; }
    public Bounds        bounds { get; set; }
    public Vector3[]     vertices { get; set; }     // ★ 标准 API
    public Vector3[]     normals { get; set; }
    public Vector4[]     tangents { get; set; }
    public Vector2[]     uv / uv2 / uv3 … uv8 { get; set; }
    public Color[]       colors { get; set; }
    public Color32[]     colors32 { get; set; }
    public Int32         vertexAttributeCount { get; }
    public Int32[]       triangles { get; set; }
    public BoneWeight[]  boneWeights { get; set; }

    // Methods（节选）
    // RVA: 0x1a4aa2ec
    public IntPtr HGGetPtrUnchecked() { }            // ★★ 鹰角自加！返回内部指针
    // RVA: 0x1a4aa498
    public VertexAttributeDescriptor GetVertexAttribute(Int32 index) { }  // ★ 顶点属性描述
    private UInt32 GetIndexCountImpl(Int32 submesh) { }
    private UInt32 GetTrianglesCountImpl(Int32 submesh) { }
    private UInt32 GetBaseVertexImpl(Int32 submesh) { }
    private Int32[] GetTrianglesImpl(Int32 submesh, Boolean applyBaseVertex) { }
    private Int32[] GetIndicesImpl(Int32 submesh, Boolean applyBaseVertex) { }
    private Array GetAllocArrayFromChannelImpl(VertexAttribute ch, VertexAttributeFormat fmt, Int32 dim) { }
    private Void  GetArrayFromChannelImpl(VertexAttribute ch, VertexAttributeFormat fmt, Int32 dim, Array values) { }
    private IntPtr GetAllBoneWeightsArray() { }
    private IntPtr GetBonesPerVertexArray() { }
    public Matrix4x4[] get_bindposes() { }
    public Void set_bindposes(Matrix4x4[] value) { }
}
```

### 40.18.2 ★★ 两条新路（比逆向序列化格式容易得多）

| # | 路 | 做法 | 难度 |
| --- | --- | --- | --- |
| **1** | **hook `Mesh.get_vertices()`** | 游戏自己会解压顶点（Unity 内部处理），**我只要 dump 出来** | ★★ |
| **2** | **调用 `HGGetPtrUnchecked()`** | 拿到 Mesh 内部指针 ⇒ 直接读顶点缓冲 | ★★ |

**⇒ 路 1 最直接：**
```
hook UnityEngine.Mesh.get_vertices（或 GetArrayFromChannelImpl）
⇒ 游戏调用时把解压后的 Vector3[] 存下来
⇒ 【立刻知道正确的顶点数据与布局】
```

**⇒ 这比逆向 `.ab` 序列化格式容易 10 倍：不用管容器、不用管压缩、不用管 stream。**

### 40.18.3 其它可用的 hook 入口

```csharp
public VertexAttributeDescriptor GetVertexAttribute(Int32 index);   // ★ 直接返回顶点属性描述
private Array GetArrayFromChannelImpl(VertexAttribute, VertexAttributeFormat, Int32, Array);
private IntPtr GetAllBoneWeightsArray();
private IntPtr GetBonesPerVertexArray();
public Matrix4x4[] get_bindposes();
```

**⇒ `GetVertexAttribute(index)` 返回的就是 `m_Channels` 的语义
（`VertexAttributeDescriptor` 含 `attribute/format/dimension/stream`）。**

### 40.18.4 ★ 重要的方法论转向

**此前 10+ 轮都在**静态逆向 `.ab` 序列化格式**，难度极高（魔改容器 + 量化压缩 + 多 stream）。**

**⇒ dump.cs 让路变简单了：**
```
游戏自己会调 Mesh.get_vertices() / GetVertexAttribute()
⇒ 我只需要【在运行时截获它】
⇒ 不需要理解序列化格式
```

**⇒ 建议：下一步改用【运行时 hook】路线（方案 B 的 payload 已有基础设施）。**

### 40.18.5 附：dump.cs 的其它价值

**`dump.cs` 是完整 il2cpp dump（81.9 MB），包含：**
```
· 全部类型 / 字段 / 方法 / RVA
· UnityEngine 的完整 API（含鹰角自加的 HG* 方法）
· 游戏逻辑类
```
**⇒ 这是**后续所有运行时 hook 的字典**。**

---

## 40.19 ★★★★★ 顶点布局的完整语义（第 64 轮，来自 dump.cs）

### 40.19.1 `VertexAttribute` 枚举（14 个通道，正好对应 `m_Channels[14]`）

```csharp
// dump.cs:1641652
public enum VertexAttribute {
    Position     = 0,   // 位置
    Normal       = 1,   // 法线
    Tangent      = 2,   // 切线
    Color        = 3,   // 顶点色
    TexCoord0    = 4,   // UV0
    TexCoord1    = 5,
    TexCoord2    = 6,
    TexCoord3    = 7,
    TexCoord4    = 8,
    TexCoord5    = 9,
    TexCoord6    = 10,
    TexCoord7    = 11,
    BlendWeight  = 12,  // 骨骼权重
    BlendIndices = 13,  // 骨骼索引
}
```

**⇒ 14 个通道 == `VertexData.m_Channels[14]` ⇒ `m_Channels[k]` 对应 `VertexAttribute` 值 k。**

### 40.19.2 `VertexAttributeFormat` 枚举（12 种格式）

```csharp
// dump.cs:1641628
public enum VertexAttributeFormat {
    Float32 = 0,    // ★★ 原始 float32
    Float16 = 1,
    UNorm8  = 2,  SNorm8  = 3,
    UNorm16 = 4,  SNorm16 = 5,
    UInt8   = 6,  SInt8   = 7,
    UInt16  = 8,  SInt16  = 9,
    UInt32  = 10, SInt32  = 11,
}
```

**⇒ `ChannelInfo.format` 就是这个枚举。**

### 40.19.3 ★★★ 这**推翻**了我之前的一个判断

```
若游戏的 Position 通道 format == Float32(0)
⇒ 顶点就是【原始 float32】！
⇒ 我此前「写入 3×float32 顶点」的做法【可能是对的】
⇒ 扭曲的原因【不是格式不匹配】，而是【多 stream 布局 / 顶点顺序】！
```

### 40.19.4 ★ 因此正确的替换流程是

```
1) 读 m_Channels[14]  ⇒ 每通道 {stream, offset, format, dimension}
2) 读 m_Streams[]     ⇒ 每 stream {offset, stride}
3) 定位 Position 通道（Attribute=0）所在 stream 与其 offset
4) 该位置按 [stream.stride] 步长写入 EFMI 的 float32 坐标
   ⇒ 【不需要量化】（若 format == Float32）
```

**⇒ 关键：先确定 `Position` 通道（k=0）的 `format` 与它所在的 `stream`。**

### 40.19.5 另一个重要推论

**`m_Streams` 多流的设计意味着：**
```
Position 在 stream 0（stride 可能是 12 或 16）
Normal   在 stream 1（stride 可能是 4/8/12）
UV       在 stream 2 …
⇒ 各 stream 在文件里是【分开的连续块】
⇒ 所以「按单一 stride 扫全区域」失败是必然的
```

**⇒ 但如果能确定 `Position` 所在 stream 的 `{offset, stride}`，
就能**只改那一段** —— 而且**可能完全不需要量化**。**

---

## 40.20 ★★★★★ hook 入口完整清单（第 65 轮，来自 dump.cs）

### 40.20.1 `Mesh` 的可 hook 方法（全部带 RVA）

```csharp
public Vector3[] get_vertices()          // ★ hook 这里拿【解压后的顶点】
public Vector3[] get_normals()
public Matrix4x4[] get_bindposes()
public Int32  get_vertexCount()
public IntPtr HGGetPtrUnchecked()        // ★ 拿内部指针
public VertexAttributeDescriptor GetVertexAttribute(Int32 index)  // ★ = m_Channels 语义
private Void GetVertexAttribute_Injected(Int32 index, out VertexAttributeDescriptor ret)
public Void SetVertexBufferParams(Int32 vertexCount, VertexAttributeDescriptor[] attributes)
private static Void SetVertexBufferParamsFromArray(IntPtr self, Int32 vertexCount, VertexAttributeDescriptor[] attributes)
```

**⇒ `GetVertexAttribute_Injected` 有 `out` 参数 ⇒ **hook 它就能拿到每个通道的布局**。**
**⇒ `VertexAttributeDescriptor` 是 `struct`（dump.cs:1642150）。**

### 40.20.2 ★ 鹰角自加的渲染器（重要）

```
HGMeshRenderer              ← ★ 鹰角自加的 Mesh 渲染器
HGMeshRendererData
HGShaderPassNames / HGShaderKeyWords / HGShaderIDs / HGShaderQualitySettings
HGUtils / HGUtilsCpp
HGPsoUtil / HGPsoWarmupCollection
```

**⇒ 确认鹰角在渲染层做了**大量魔改**。
**⇒ `HGMeshRenderer` 可能是顶点数据的实际持有者。**
**⇒ `Mesh.HGGetPtrUnchecked()` 更是直接暴露内部指针。**

### 40.20.3 ★ 三条 hook 路线

| 路 | hook 点 | 得到 |
| --- | --- | --- |
| **A** | **`Mesh.get_vertices()`** | **解压后的 `Vector3[]` ⇒ 直接知道正确坐标** |
| **B** | **`Mesh.GetVertexAttribute(index)`** | **每通道的 `VertexAttributeDescriptor` ⇒ 直接知道顶点布局** |
| **C** | **`HGMeshRenderer` 的某方法** | **可能更早拿到原始缓冲** |

**⇒ 路 A + B 组合就是完美答案：既知道布局，又知道数据。**

### 40.20.4 ★ 为什么运行时 hook 优于静态逆向（总结）

```
静态逆向已投入 13 轮：
  ✅ 结构完全查明（CompressedMesh / PackedFloatVector / VertexData /
     StreamInfo / ChannelInfo / VertexAttribute / VertexAttributeFormat）
  ❌ 但【定位失败】：魔改容器 + 量化 + 多 stream ⇒ 找不到实际数据

运行时 hook：
  ✅ 游戏自己会调用 get_vertices() / GetVertexAttribute()
  ✅ 只需截获 ⇒ 不需要理解序列化格式
  ✅ 基础设施已有（方案 B 的 zygisk payload）
```

### 40.20.5 ⚠️ 已知风险（此前记录）

```
· 自制的 efml4a_zygisk 模块【曾让 ZygiskNext 全局崩溃】
  ⇒ 修复方式: ksud module uninstall + 真重启
· zygisk_il2cppdumper 曾【每次启动崩溃游戏】
  ⇒ 已清除 persist 属性 + 注释 service.sh + 加 disable 文件
⇒ 【再次上 hook 要格外小心，先做最小验证】
```

---

## 40.21 ★★★ 更干净的 hook 点：`HGMeshRendererData.GetMeshes()`（第 66 轮）

### 40.21.1 定义（`dump.cs:2005284` / `2005304`）

```csharp
// Dll : UnityEngine.HGGraphicsModule.dll   Namespace: UnityEngine
public sealed class HGMeshRenderer : Behaviour {
    public HGMeshRendererData data { get; set; }   // RVA 0x1a510ef0
    public Entity GetEntity() { }                  // RVA 0x1a510f70
    private Void GetEntity_Injected(out Entity ret) { }
}

public sealed class HGMeshRendererData : Object {
    public Material[] GetMaterials() { }           // RVA 0x1a5110c8
    public Void SetMaterials(Material[] materials) { }
    public Mesh[] GetMeshes() { }                  // ★★ RVA 0x1a51114c
}
```

### 40.21.2 ★ 为什么这个 hook 点更好

```
hook Mesh.get_vertices()           → 所有 Mesh 都会触发（噪声大）
hook HGMeshRendererData.GetMeshes() → 只在【实际渲染】时触发
                                      ⇒ 能定位到具体角色/部件
```

### 40.21.3 ★ 最终 hook 方案（已定）

```
① hook HGMeshRendererData.GetMeshes()   → 拿到 Mesh[] 实例
② 对每个 Mesh 调 get_vertexCount() / get_vertices()   → 拿顶点
③ 调 GetVertexAttribute(0..13)          → 拿通道布局
④ 全部写日志文件（【不修改任何东西，纯只读】）
```

**⇒ 第一阶段**纯只读**，不写内存 ⇒ **零崩溃风险**。**
**⇒ 拿到正确的顶点数据与布局后，再规划回写方案（C 工具）。**

### 40.21.4 ★ RVA 清单（下次 hook 用）

| 方法 | RVA |
| --- | --- |
| `Mesh.HGGetPtrUnchecked()` | `0x1a4aa2ec` |
| `Mesh.GetVertexAttribute(Int32)` | `0x1a4aa498` |
| `Mesh.get_vertexCount()` | （dump.cs 内） |
| `Mesh.get_vertices()` | （dump.cs 内） |
| `Mesh.get_bindposes()` | `0x1a4ab220` |
| `HGMeshRenderer.get_data()` | `0x1a510ef0` |
| `HGMeshRenderer.GetEntity()` | `0x1a510f70` |
| `HGMeshRendererData.GetMeshes()` | `0x1a51114c` |
| `HGMeshRendererData.GetMaterials()` | `0x1a5110c8` |

**⚠️ RVA 是**1.6.4** 的（dump.cs 日期 2026-08-13，但游戏已更新到 1.6.4）
⇒ 实际使用前需重新校验 RVA（`docs/34` §34.4 已记录 18 个 RVA 的变化）。**

### 40.21.5 阶段性总结（第 2 阶段·收尾）

| 项 | 状态 |
| --- | --- |
| **mod 能加载 + 游戏内可见变化** | ✅ **已达成**（1.5.3 + 1.6.4） |
| **静态逆向 `.ab`** | ✅ 结构完全查明；❌ 定位失败（魔改容器+量化+多 stream） |
| **顶点语义** | ✅ 完全清楚（14 通道 + 12 格式枚举） |
| **运行时 hook 路线** | 🟢 **入口已定位（RVA 清单已列）** |
| **字典** | ✅ `dump.cs` 81.9 MB |
| **自有工具（全 C）** | ✅ 7 个 |

**⇒ 「差一步」的原因链条已完全清楚：
不是「格式不匹配」，而是「顶点数据流是**多 stream 分离**的，
而我们在错误的偏移做了**整体覆盖**」。**

---

## 40.22 ★★★★★ 扭曲的核心原因：**坐标系根本不同**（第 41 轮）

> **来源**：重读自己的文档（用户建议）后得到的启发 + 新的定量验证。

### 40.22.1 决定性数据

**目标 Mesh（`e2cd202a`，我们在改的那个）—— **局部坐标**：**

```
m_Center = (-0.1942, 0.0033, -0.0368)     ← 都在【原点附近】
m_Extent = ( 0.0455, 0.1829,  0.0445)
⇒ 尺寸 0.0910 × 0.3658 × 0.0890
⇒ Y 是 X/Z 的约 4 倍 ⇒ 细长部件
```

**EFMI 所有组件（从 `VB0` 实测 AABB）—— **世界坐标**（Z-up）：**

| 组件 | min | max | 尺寸 |
| --- | --- | --- | --- |
| **Component20** | (-0.082, -0.107, **1.464**) | (0.082, -0.049, **1.597**) | 0.165×0.058×0.133 |
| Component10 | (-0.088, -0.091, 1.431) | (0.088, 0.033, 1.620) | 0.177×0.123×0.189 |
| **Component0** | (-0.161, -0.101, 1.355) | (0.163, 0.162, 1.679) | 0.324×0.263×0.324 |
| Component30 | (-0.356, -0.125, 0.964) | (0.300, 0.048, 1.412) | 0.656×0.172×0.447 |
| Component100 | (-0.392, -0.134, 0.620) | (0.392, 0.173, 1.129) | 0.785×0.307×0.508 |
| Component50 | (-0.506, -0.112, 0.751) | (0.506, 0.094, 1.528) | 1.012×0.206×0.777 |
| **Component90** | (-0.418, -0.208, **-0.007**) | (0.417, 0.178, **1.713**) | 0.835×0.386×**1.720** |

**⇒ 【所有 EFMI 组件的 Z 都在 0.6 ~ 1.7】⇒ Z 是【身高方向】**
**⇒ 【X ±0.5, Y ±0.2, Z 0~1.71】= 完整角色站在原点（脚在 Z=0，头顶 Z=1.71）**
**⇒ `Component90` 的 Z 跨度 1.720 ≈ 标准身高 ⇒ 那是整身**

### 40.22.2 ★★★ 结论

| | 坐标系 | 含义 |
| --- | --- | --- |
| **EFMI 组件** | **世界坐标**（Z-up，脚在 Z=0） | 整个角色 |
| **目标 Mesh** | **局部坐标**（`m_Center` 在原点附近，Y 最长） | **某骨骼下的部件** |

**⇒ **用世界坐标的顶点覆盖局部坐标的 Mesh ⇒ 必然错乱！****
**⇒ 这比「压缩格式」「多 stream」都更根本 —— 即使顶点布局完全正确，坐标系不对照样扭曲。**

### 40.22.3 尺寸匹配结果（也印证了这一点）

```
按「排序后尺寸」比较（允许轴重排）:
  最接近的是 Component20（err=0.0913），但所有候选的 err 都 > 0.09
  ⇒ 【没有任何 EFMI 组件与目标部件尺寸匹配】
  ⇒ 因为坐标系不同，尺寸比较本身就没有意义
```

### 40.22.4 ⏭️ 修复方向（明确了）

```
① 把 EFMI 的世界坐标顶点【变换到目标 Mesh 的局部空间】
   · 减去 m_Center（(-0.1942, 0.0033, -0.0368)）
   · 或应用所属骨骼的 bind pose 逆矩阵（更正确）
② 或者：找 EFMI 里【局部坐标】的组件（若有）
③ 同时：把 m_CompressionType 改 0 + 写未压缩 float32（§28.35.4）
④ 索引缓冲一起处理（docs/31）
```

### 40.22.5 ★ 方法论教训（重要）

```
我在第 32 轮就写下「不能直接用 EFMI 的整身组件去覆盖部件」（§32.4），
但后续 30+ 轮完全跑偏到「顶点压缩格式」上，忘了这条。

⇒ 教训：重大结论要【定期回顾】，不能只往前冲。
⇒ 用户建议「重读自己的文档」直接导致了本轮的突破。
```

### 40.22.6 ⚠️ 待验证

```
· 目标 Mesh 的局部坐标属于【哪根骨骼】？（需要 m_BindPose + m_BoneNameHashes）
· 该 Mesh 的骨属关系可通过 SkinnedMeshRenderer.bones 确定
· 若能拿到 bind pose 逆矩阵 ⇒ 可把 EFMI 世界坐标精确变换到局部空间
```
