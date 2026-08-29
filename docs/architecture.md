# SemiStreamProbe 架构设计

本文说明当前实现和 `v0.1.0` 收口目标。文中使用“当前”描述已经存在的代码，使用
“目标”描述路线图中尚待实现的结构，避免把设计意图误写成现有能力。

## 1. 目标与边界

SemiStreamProbe 是同步、确定性、无第三方运行时依赖的 H.264/RTP 码流诊断工具。

它负责：

- 识别 H.264 Annex-B、NAL 和必要的语法结构；
- 解析 RTP 与 RFC 6184 负载；
- 建立参数集、Access Unit、GOP 和 RTP 会话统计；
- 把异常转换成带证据、可能影响和恢复条件的诊断；
- 通过文本和 JSON 对外提供同一份分析结果。

它不负责解码、渲染、音画同步、重传、拥塞控制或通用网络播放。

## 2. 依赖方向

```text
CLI
 │
 ▼
Application ──→ Input adapters
 │                 │
 └────────┬────────┘
          ▼
         Core
```

- `CLI`：解析命令行、选择任务、打印结果并映射退出码；
- `Application`：编排一次检查或监听任务，选择报告格式；
- `Input adapters`：文件读取、UDP socket 和到达时间采集；
- `Core`：纯字节解析、状态模型、统计和诊断，不依赖 CLI 或操作系统 API。

依赖只能指向 Core。Core 不知道输入来自文件、UDP、测试字节数组还是未来的其他适配器。

## 3. 两条数据链路

### 3.1 Annex-B 文件

```text
File bytes
  ↓
AnnexBScanner
  ↓
NAL / RBSP / SPS / PPS / Slice
  ↓
ParameterSetRegistry / AccessUnitAssembler / GopStatistics
  ↓
AnalysisReport
  ↓
TextReporter / JsonReporter
```

当前已经接通统一 `AnalysisReport`、文本 Reporter 和 JSON Reporter。

### 3.2 UDP/RTP/H.264

```text
UDP datagram + monotonic arrival time
  ↓
RtpPacketParser
  ↓
RtpStreamAnalyzer
  ├── sequence / duplicate / reorder / jitter
  └── active SSRC state
  ↓
H264RtpDepacketizer
  ├── Single NAL Unit
  ├── STAP-A
  └── FU-A Reassembler
  ↓
NAL / H.264 stream model
  ↓
AnalysisReport + Diagnostics
```

当前已经实现跨平台 UDP 接收、RTP 包解析、确定性 `RtpStreamAnalyzer`、三类 RFC 6184
负载处理、FU-A/IDR 流诊断，以及 H.264 语法模型和统一报告接线。`v0.1.0` 的实现、验证
和 Windows 便携包验收已经完成，只待发布 GitHub Release。

UDP socket 使用 4 MiB 接收队列，吸收 1080p 帧在 FU-A 分片后的短时突发；这只减少应用
来不及调用 `recv` 导致的本机队列溢出，不掩盖已经到达 RTP 分析器的真实序列缺口。

## 4. 当前目录职责

```text
include/semi_stream_probe/core/
  types.hpp          基础字节类型和 ByteView
  parse_error.hpp    结构化解析错误
  diagnostic.hpp     稳定诊断码、严重程度、证据与位置
  annex_b.hpp        Annex-B 扫描
  nal.hpp            NAL Header
  rbsp.hpp           EBSP 到 RBSP
  bit_reader.hpp     位读取与 Exp-Golomb
  h264_syntax.hpp    SPS/PPS 数据模型与解析
  parameter_sets.hpp SPS/PPS 注册与查询
  slice.hpp          基础 Slice Header
  access_unit.hpp    Access Unit 组装
  gop.hpp            Slice 类型与 GOP 统计
  rtp.hpp            RTP 包解析
  rtp_stream_analyzer.hpp  单 SSRC/PT 连续性统计与 jitter
  h264_rtp.hpp       Single NAL/STAP-A 解包与 FU-A 重组
  h264_rtp_stream_analyzer.hpp  RFC 6184 流分析、完整 NAL 与 FU-A/IDR 诊断
  h264_stream_model.hpp  增量参数集、Slice、Access Unit 和 GOP 模型

include/semi_stream_probe/application/
  inspect.hpp        Annex-B 检查任务与原始解析结果
  listen.hpp         UDP/RTP/H.264 监听任务
  report.hpp         统一报告模型及文本/JSON Reporter
  rtp_analysis.hpp   RTP/H.264 核心统计到统一报告的映射

include/semi_stream_probe/infrastructure/
  udp_receiver.hpp   跨平台 UDP 端点解析和 datagram 接收

src/core/            Core 接口实现
src/application/     文件检查与 UDP 监听编排
src/infrastructure/  Winsock/POSIX socket 适配
src/cli/             命令行入口
tests/               无第三方测试框架的确定性测试
```

收口期间允许按职责增加会话分析、诊断、报告和 UDP 适配代码，但不会为了预想中的扩展
提前建立通用媒体框架。

## 5. 数据所有权

### 5.1 借用视图

`NalUnitRef` 保存输入缓冲区中的偏移和长度，不拥有字节。解析器接收 `ByteView`，调用方
必须保证底层缓冲区在解析期间有效。

RTP Header、Single NAL 和 STAP-A 返回的 `ByteView` 同样借用调用方持有的 UDP datagram。
这些视图不得进入跨包状态或最终报告。

### 5.2 跨包状态

FU-A 重组器拥有尚未完成的 NAL 缓冲区。完成后返回拥有字节的值对象，错误或显式重置
必须释放不完整数据。

RTP 会话分析器只保存统计、序列状态、必要的参数集摘要和 FU-A 重组状态，不无限保留
原始数据包。所有集合和重组缓冲都必须有明确上限。

### 5.3 报告

`AnalysisReport` 只保存值类型摘要。它不保存指向输入文件或 UDP datagram 的视图，因此
可以在输入缓冲释放后继续渲染为文本或 JSON。

## 6. 错误与诊断边界

### 6.1 ParseError

解析函数使用 `std::expected<T, ParseError>` 表示当前结构无法产生合法结果。
`ParseError` 保存：

- 错误码；
- 字节和比特偏移；
- NAL 索引；
- RTP 序列号；
- 简短技术信息。

Annex-B 文件的关键语法无法继续解析时，应用层可以让整个检查任务失败。

### 6.2 Diagnostic

目标 `Diagnostic` 表示会话级、可以记录后继续分析的问题：

```text
severity + stable code + summary
location/evidence + possible impact + recovery
```

例如，FU-A 序列号中断会产生诊断、丢弃当前不完整 NAL 并重置重组器，但不会终止 UDP
监听。后续合法的 FU-A 起始片可以建立新状态。

同一个底层错误在不同上下文中的影响可能不同：普通非参考 NAL 与 IDR NAL 的分片丢失
不能机械地输出相同结论。因此 Core 先保留事实，诊断规则再结合 NAL 类型和会话状态生成
影响与恢复说明。

## 7. RTP 会话状态

目标 `RtpStreamAnalyzer` 接收完整 datagram 和单调时钟到达时间。首版只分析一个活动
SSRC：未显式配置时由首个合法媒体包锁定，其他 SSRC 产生诊断并忽略。分析器维护：

- 配置的 Payload Type 和时钟频率；
- 最近接收序列号及回绕状态；
- 接收、丢失、重复和乱序数量；
- RFC 3550 interarrival jitter 所需的 transit 状态；
- 当前 FU-A 重组状态；
- H.264 参数集和最近完整 IDR 信息；
- 已累计的诊断与受限证据。

首版不实现通用乱序缓冲。一个迟到包可以被识别和统计，但不会回填已经丢弃的 FU-A。
这是诊断工具的明确策略，不代表播放器或 SemiLive 接收端未来必须采用相同策略。

## 8. 参数集与 Access Unit

SPS/PPS 可以先从文件中独立提取，但只按原始 NAL 顺序激活。Slice 始终使用它所在位置
已经激活的参数集版本，不能使用文件末尾最终留存的同 ID 定义。

Annex-B 和 RTP 解包最终都产生 NAL，因此应复用参数集注册、Slice 解析和 Access Unit
组装逻辑。输入适配层不得复制一套 H.264 语法实现。

## 9. 报告边界

分析器只生成统一 `AnalysisReport`，Reporter 负责序列化：

```text
AnalysisReport
  ├── input/session metadata
  ├── H.264 summary
  ├── RTP summary
  ├── access-unit/GOP statistics
  └── diagnostics[]
          ↓
     Text / JSON
```

Reporter 不重新判断丢包、IDR 影响或恢复条件，避免文本和 JSON 对相同输入给出不同结论。
JSON 字段已在 [诊断与报告契约](report-contract.md) 中冻结，后续通过 golden tests 验证。

## 10. 跨平台与运行模型

Core 只使用 C++ 标准库中的固定宽度整数、`std::span`、`std::vector`、`std::expected`
和基础字符串、时间类型。

UDP 的 Windows socket 与 POSIX socket 差异只存在于输入适配器。UDP 接收循环可以阻塞，
但每个 datagram 进入 Core 后按顺序同步分析；首版不需要在线程之间共享分析状态。

CMake 是构建工具，不属于程序运行时依赖。测试继续优先使用小型、确定性的字节样本；
真实 socket 只验证适配边界，不承担协议规则的主要覆盖。

## 11. 实现约束

- 所有长度、偏移和算术在使用前检查溢出；
- 不可信输入不得触发断言、越界访问或无上限分配；
- 会话错误必须明确选择“继续、重置局部状态或终止任务”；
- 诊断码一旦进入 `v0.1.0` JSON 契约，不因文案修改而改变；
- 新增能力必须先有确定性 Core 测试，再接 CLI 或 UDP；
- 首版范围和完成条件以 [项目路线图](roadmap.md) 为准。
