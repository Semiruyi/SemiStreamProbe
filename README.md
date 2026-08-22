# SemiStreamProbe

## 当前状态

项目当前已完成第一个可运行的 Annex-B 检查链路，包括：

- `semi_stream_probe_core` 核心静态库；
- `semi_stream_probe_application` 应用层静态库；
- `semistreamprobe` CLI 入口；
- 三字节/四字节 Annex-B Start Code 扫描；
- NAL Header 解析和非法 `forbidden_zero_bit` 检查；
- `inspect <file.h264> [--nal-list]` 文件检查；
- Annex-B、NAL、应用层和编译冒烟测试。

SPS/PPS、Slice 和 RTP 解析仍按后续里程碑逐步实现。架构说明见
[docs/architecture.md](docs/architecture.md)。

## 构建骨架

项目只需要 CMake、C++23 编译器和标准库。当前环境可以直接使用 MinGW Preset：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

Release 构建：

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

Preset 假定 `g++` 和 `ninja` 已经位于 `PATH` 中。Visual Studio 或其他工具链可以
后续增加独立的 Preset，不需要修改核心代码。

查看 CLI 帮助：

```powershell
.\build\mingw-debug\semistreamprobe.exe --help
```

SemiStreamProbe 是一个使用 C++23 开发的 H.264/RTP 码流诊断工具。它面向音视频开发、
测试和故障排查场景，目标是把“花屏、卡顿、无法起播”等现象转换为可观察、可复现的
码流证据。

> 项目处于规划与早期开发阶段。本文中的命令和输出描述的是 `v0.1.0` 的目标能力，
> 尚未实现的内容会在路线图中明确标注。

## 为什么做这个项目

播放器或解码器通常只能告诉使用者“解码失败”，却很难直接回答：

- 输入是否缺少 SPS/PPS？
- Slice 是否引用了不存在的参数集？
- IDR 是否在 RTP 分片传输过程中丢包？
- 当前花屏能否自行恢复，需要等待多久？
- 这是编码码流问题，还是网络丢包、乱序或时间戳问题？

SemiStreamProbe 将 H.264 语法分析和 RTP 传输诊断连接起来，输出异常、影响、证据和
可能的恢复条件。项目也用于系统学习 H.264、RFC 6184 和实时音视频问题定位。

## 项目目标

- 原生解析 H.264 Annex-B、NAL Unit、SPS、PPS 和最小 Slice Header。
- 统计分辨率、Profile、Level、帧类型、关键帧间隔和 GOP 结构。
- 解析 RTP 固定头，并支持 H.264 Single NAL、STAP-A 和 FU-A 解包。
- 检测丢包、乱序、重复包、时间戳异常和不完整 FU-A。
- 将协议异常关联到解码影响和恢复边界。
- 提供适合人工阅读的文本报告和适合自动化处理的 JSON 报告。
- 对截断、损坏和不可信输入保持内存安全，返回包含位置和上下文的结构化错误。

## 非目标

首个版本不会实现：

- 完整 H.264 解码器；
- 视频渲染或播放器界面；
- H.265、AV1、AAC 等其他编解码格式；
- 完整 RTSP 或 WebRTC 协议栈；
- 音视频同步、硬件解码或 GPU 处理；
- 通用 MP4、FLV、MKV 解封装器。

FFmpeg 可以作为后续的输入适配器、测试媒体生成工具和结果对照，但核心 H.264 语法解析
与 RTP/H.264 重组不会委托给 FFmpeg。

## 目标使用方式

### 检查 H.264 Annex-B 裸流

```powershell
semistreamprobe inspect sample.h264
```

目标输出：

```text
Codec: H.264/AVC
Resolution: 1920x1080
Profile: High
Level: 4.2
NAL units: 18342
Access units: 600
IDR frames: 5
Average GOP: 120 frames
SPS/PPS changes: 0
Diagnostics: 0 errors, 0 warnings
```

### 查看逐 NAL 信息

```powershell
semistreamprobe inspect sample.h264 --nal-list
```

目标输出：

```text
OFFSET      SIZE    TYPE       REF  FRAME
0x00000000  28      SPS        3    -
0x00000020  8       PPS        3    -
0x0000002C  18452   IDR_SLICE  3    0
```

### 监听 RTP/H.264

```powershell
semistreamprobe listen --udp 0.0.0.0:5004 --payload-type 96
```

目标诊断：

```text
[ERROR] Incomplete FU-A at RTP sequence 12031.
Evidence: one packet is missing while reconstructing an IDR NAL unit.
Impact: the current access unit cannot be reconstructed safely.
Recovery: visible corruption may continue until the next intact IDR.
```

### 输出 JSON 报告

```powershell
semistreamprobe inspect sample.h264 --output json
```

JSON 输出用于回归测试、批量巡检和其他工具集成。字段结构会在首个可用版本中确定，
`v0.x` 阶段不承诺跨版本兼容。

## 计划架构

```text
Input
├── AnnexBFileSource
├── UdpRtpSource
└── PcapSource                  (future)
        │
        ▼
Transport / Framing
├── AnnexBScanner
├── RtpPacketParser
└── H264RtpDepacketizer
        │
        ▼
H.264 Syntax
├── EbspToRbsp
├── BitReader
├── ExpGolombReader
├── NalHeaderParser
├── SpsParser
├── PpsParser
└── SliceHeaderParser
        │
        ▼
Stream Model
├── ParameterSetRegistry
├── AccessUnitAssembler
├── GopTracker
└── StreamStatistics
        │
        ▼
Diagnostics
├── SyntaxRules
├── ParameterSetRules
├── TimestampRules
└── RtpContinuityRules
        │
        ▼
Report
├── TextReporter
└── JsonReporter
```

RTP/H.264 解包器的输出仍然是 NAL Unit，因此文件输入和网络输入会复用同一套 H.264
语法解析、流模型和诊断规则。

## 设计原则

### 不信任输入

码流和网络包都被视为不可信数据。解析器不会依赖断言处理输入错误，也不会在数据不足时
越界读取。预期采用 `std::expected<T, ParseError>` 返回错误，并保留：

- 错误码；
- 字节或比特偏移；
- NAL 索引或 RTP 序列号；
- 用于定位问题的简短上下文。

### 核心组件保持确定性

语法解析、RTP 解包、流模型和诊断规则首先实现为同步、可重复测试的组件。实时 UDP 输入
只负责提供数据，不把网络时序和并发状态扩散到核心解析器。

### 先提供证据，再给出结论

诊断结果至少包含严重程度、错误码、解释、影响、恢复条件和原始证据。工具不会仅凭单个
异常包武断地声称画面一定出现某种故障。

### 测试关注边界而不是数量

重点覆盖截断输入、Start Code 边界、Exp-Golomb 极值、参数集引用错误、RTP 序列号
回绕、重复与乱序，以及 FU-A 丢首片、丢中片和丢尾片等情况。

## 路线图

### Milestone 0：工程基线

- [x] C++23、CMake 和无第三方依赖的冒烟测试入口
- [x] CLI、错误模型和测试媒体约定的接口骨架
- [x] 基础项目文档
- [ ] Windows 构建与持续集成

### Milestone 1：Annex-B 与 NAL

- [x] 三字节和四字节 Start Code
- [x] NAL Header 解析
- [ ] VCL/非 VCL 分类与统计
- [ ] 截断、空 NAL 和非法 `forbidden_zero_bit` 诊断
- [x] 逐 NAL 文本输出

### Milestone 2：参数集

- [x] EBSP 到 RBSP 转换
- [x] 位读取与有符号/无符号 Exp-Golomb
- [ ] SPS/PPS 解析
- [ ] 分辨率、Profile、Level、色度格式和位深
- [ ] cropping 与基础 VUI 字段

### Milestone 3：Slice 与 GOP

- [ ] 最小 Slice Header
- [ ] 参数集注册与引用检查
- [ ] Access Unit 组装
- [ ] I/P/B、IDR 间隔和 GOP 统计
- [ ] 参数集变化诊断

### Milestone 4：RTP/H.264

- [ ] RTP Header、扩展字段、SSRC、序列号和时间戳
- [ ] Single NAL Unit
- [ ] STAP-A
- [ ] FU-A 重组
- [ ] 丢包、乱序、重复包和序列号回绕
- [ ] 不完整 FU-A 与 IDR 丢包影响分析
- [ ] RFC 3550 interarrival jitter 估计

### Milestone 5：诊断与发布

- [ ] 文本和 JSON 报告
- [ ] 至少 10 类结构化诊断
- [ ] 正常与故障注入样本
- [ ] 与 FFmpeg/ffprobe 结果对照
- [ ] 模糊测试
- [ ] Windows x64 Release 包
- [ ] 一分钟故障诊断演示

### 后续探索

- [ ] 离线 PCAP + SDP 分析
- [ ] AVCC 输入
- [ ] 最小 RTSP 客户端或 FFmpeg RTSP 输入适配器
- [ ] Linux 构建与 CI
- [ ] H.265/RTP
- [ ] HTML 可视化报告

## `v0.1.0` 完成标准

首个可发布版本需要满足：

- 支持 H.264 Annex-B 裸流；
- 支持 SPS、PPS 和最小 Slice Header；
- 支持 Single NAL、STAP-A 和 FU-A；
- 支持实时 UDP 或确定性的离线 RTP 输入；
- 提供至少 10 类结构化诊断；
- 提供文本与 JSON 输出；
- 包含正常样本和故障注入测试；
- 发布可直接运行的 Windows x64 包；
- README 提供可在一分钟内完成的演示；
- 能用诊断证据解释 IDR 的 FU-A 中间分片丢失后的影响和恢复边界。

## 构建

当前 MinGW 工作流为：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

## 参考标准

- ITU-T H.264：Advanced video coding for generic audiovisual services
- RFC 6184：RTP Payload Format for H.264 Video
- RFC 3550：RTP: A Transport Protocol for Real-Time Applications

实现会记录所依据的标准章节。测试媒体和抓包样本将同时记录来源、生成方式与授权信息，
避免无法复现或许可证不明确的二进制素材进入仓库。

## 许可证

许可证将在首次发布前确定。在许可证确定之前，请不要假定本仓库内容允许复制、修改或
重新分发。
