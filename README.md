# SemiStreamProbe

SemiStreamProbe 是一个使用 C++23 开发的 H.264/RTP 码流诊断工具。它把“花屏、卡顿、
无法起播”等现象转换为可以定位和复现的码流证据，同时用于系统学习 H.264、RFC 6184
和实时音视频故障分析。

项目正在收口 `v0.1.0`。H.264 Annex-B 语法解析和 RTP/H.264 负载处理的核心组件已经
完成；RTP 会话分析、结构化诊断、JSON、实时 UDP 入口和发布材料尚未完成。

## 当前可用能力

- 扫描三字节和四字节 Annex-B Start Code；
- 解析 NAL Header，并检查非法 `forbidden_zero_bit`；
- 完成 EBSP 到 RBSP 转换、位读取和 Exp-Golomb 解码；
- 解析 SPS、PPS 和基础 Slice Header（不解析宏块数据）；
- 按 NAL 顺序激活和替换参数集，并检查 Slice 参数集引用；
- 按首个 VCL NAL 规则组装 Access Unit，支持一帧多 Slice 和 AUD；
- 统计 I/P/B/SP/SI、混合 Slice、IDR 位置和 IDR 间隔；
- 解析 RTP v2 固定头、CSRC、扩展头和 padding；
- 解析 RFC 6184 Single NAL Unit、STAP-A 和 FU-A，并重组连续的 FU-A 分片；
- 通过 CLI 输出 Annex-B 文件摘要和逐 NAL 列表。

当前 CLI 只接通 Annex-B 文件检查：

```powershell
semistreamprobe inspect sample.h264
semistreamprobe inspect sample.h264 --nal-list
```

示例输出：

```text
Codec: H.264/AVC
Resolution: 1920x1080
Profile: High
Level: 4.2
NAL units: 18342
Slices: 600
Access units: 600
IDR access units: 5
IDR interval: 120.0 AU average (min 120, max 120)
```

## v0.1.0 范围

`v0.1.0` 只完成两条输入链路：

```text
Annex-B file ──→ H.264 syntax/model ──→ report

UDP/RTP ──→ RTP session analysis ──→ RFC 6184 depacketization
        ──→ H.264 syntax/model ──→ diagnostics/report
```

版本收口包含：

- Annex-B 文件检查；
- RTP/H.264 UDP 监听；
- 丢包、乱序、重复包、序列号回绕和 RFC 3550 jitter 统计；
- 不完整 FU-A、IDR 分片丢失及恢复边界诊断；
- 文本与 JSON 报告；
- 正常流与故障注入样本；
- Windows/Linux CI、解析器安全检查和 Windows x64 Release；
- 可在一分钟内复现的故障诊断演示。

冻结后的 CLI 契约和逐项验收条件见 [项目路线图](docs/roadmap.md)。内部边界和数据流见
[架构设计](docs/architecture.md)。

## 非目标

首个版本不会实现：

- 完整 H.264 解码器或播放器；
- H.265、AV1、AAC 等其他编解码格式；
- RTSP、WebRTC、RTCP、重传、FEC 或拥塞控制；
- PCAP、AVCC、MP4、FLV、MKV 等输入；
- 通用乱序缓冲、视频渲染、音画同步或硬件解码；
- HTML 可视化报告。

FFmpeg 可以用于生成测试媒体和交叉验证，但核心 H.264 语法解析、RTP 解析和 RFC 6184
负载处理不会委托给 FFmpeg。

## 设计原则

### 不信任输入

码流和网络包都被视为不可信数据。解析器在数据不足时不会越界读取，并通过
`std::expected<T, ParseError>` 返回包含字节、比特、NAL 或 RTP 位置的错误。

### 核心保持确定性

语法解析、RTP 解包、会话统计和诊断规则保持同步且可重复测试。UDP 入口只负责收包和
记录到达时间，不把 socket、线程或操作系统类型带入 Core。

### 先提供证据，再给出结论

诊断包含严重程度、稳定错误码、证据、可能影响和恢复条件。工具不会只根据一个异常包
断言画面一定出现特定故障。

### 以边界测试为主

测试重点覆盖截断输入、Start Code 边界、Exp-Golomb 极值、参数集引用错误、RTP 序列号
回绕、重复与乱序，以及 FU-A 丢首片、丢中片和丢尾片。

## 构建

项目只需要 CMake、C++23 编译器和标准库。当前 MinGW 工作流为：

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

Preset 假定 `g++` 和 `ninja` 已位于 `PATH`。查看 CLI 帮助：

```powershell
.\build\mingw-debug\semistreamprobe.exe --help
```

## 文档

| 文档 | 内容 |
|---|---|
| [架构设计](docs/architecture.md) | 依赖方向、数据所有权、错误与诊断边界、目标数据流 |
| [项目路线图](docs/roadmap.md) | v0.1.0 冻结范围、CLI 契约、实现顺序和完成标准 |
| [测试样本](samples/README.md) | 本地媒体生成方式和样本许可证约束 |

## 参考标准

- ITU-T H.264：Advanced video coding for generic audiovisual services；
- RFC 6184：RTP Payload Format for H.264 Video；
- RFC 3550：RTP: A Transport Protocol for Real-Time Applications。

实现和测试会记录所依据的标准章节。二进制样本进入仓库前必须记录来源、生成方式和
再分发许可证。

## 许可证

项目许可证将在 `v0.1.0` 发布前确定。在许可证确定之前，请不要假定本仓库内容允许
复制、修改或重新分发。
