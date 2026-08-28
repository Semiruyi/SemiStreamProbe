# SemiStreamProbe v0.1.0 路线图

本文冻结 `v0.1.0` 的范围、命令行契约和完成条件。版本收口期间不增加新的编解码格式、
容器或会话协议；未列入本文的功能默认推迟到首版之后。

## 1. 当前基线

已完成：

- C++23、CMake、CLI 和无第三方测试框架的工程基线；
- Annex-B、NAL Header、EBSP/RBSP、BitReader 和 Exp-Golomb；
- SPS、PPS、基础 Slice Header 和参数集按顺序激活；
- Access Unit 组装、Slice 类型与 IDR 间隔统计；
- RTP Header、CSRC、扩展头和 padding；
- RFC 6184 Single NAL Unit、STAP-A 和 FU-A；
- FU-A 连续分片重组、序列号回绕和错误状态清理；
- Annex-B 摘要、逐 NAL 列表、统一报告模型和文本/JSON 输出；
- 稳定的 `Diagnostic` 类型、严重程度和首版诊断码；
- 跨平台 UDP 接收、单调时钟到达时间和 `listen --udp`；
- `--version` 及当前 CLI 的稳定退出码。

尚未闭环：

- 尚无完整故障注入样本、交叉验证、CI 和 Release。

## 2. 冻结范围

### 输入

- H.264 Annex-B 文件；
- 单路 UDP/RTP/H.264 和单个 SSRC；
- 未显式配置 SSRC 时，以首个合法媒体包的 SSRC 为当前流；其他 SSRC 产生诊断并忽略。

### H.264 与 RTP

- H.264 SPS、PPS、基础 Slice Header、Access Unit 和 GOP；
- RTP v2 与动态 Payload Type；
- RFC 6184 Single NAL Unit、STAP-A 和 FU-A；
- 90 kHz 视频时钟为默认值，允许通过 CLI 覆盖；
- 检测序列号间断、重复、乱序和 16 位回绕；
- 按 RFC 3550 估计 interarrival jitter；
- FU-A 出错后丢弃当前不完整 NAL，并允许后续合法起始分片恢复。

首版只诊断乱序，不提供通用重排缓冲。诊断只能说明证据支持的可能影响，不能仅凭网络
异常断言实际画面一定花屏。

### 输出

- 适合人工阅读的文本报告；
- 适合自动处理的 JSON 报告；
- 两种格式来自同一个报告模型，字段语义保持一致；
- 摘要、统计和诊断使用稳定的机器可读错误码。

## 3. CLI 契约

当前接口：

```text
semistreamprobe inspect <file.h264>
    [--nal-list]
    [--output text|json]

semistreamprobe listen --udp <address:port>
    --payload-type <0..127>
    [--clock-rate <hz>]
    [--duration <seconds>]
    [--output text|json]

semistreamprobe --help
semistreamprobe --version
```

约定：

- `--output` 默认为 `text`；
- H.264 视频的 `--clock-rate` 默认为 `90000`；
- `--duration` 缺省时持续监听，直到收到退出信号；
- 正常报告写入标准输出，参数和运行错误写入标准错误；
- 退出码 `0` 表示分析任务完成，即使报告包含流质量诊断；
- 退出码 `1` 表示输入、I/O 或运行时失败；
- 退出码 `2` 表示命令行用法错误。

## 4. 诊断基线

首版至少覆盖以下可复现诊断类别：

1. RTP Header、extension 或 padding 非法；
2. RTP Payload Type 或 SSRC 与配置不一致；
3. RTP 序列号间断；
4. RTP 重复包；
5. RTP 乱序包；
6. H.264/RTP 负载类型或字段非法；
7. STAP-A 长度、空 NAL、嵌套类型或 NRI 约束非法；
8. FU-A continuation/end 缺少起始片；
9. FU-A 中间分片丢失；
10. 前一个 FU-A 未完成便出现新的起始片；
11. 输入结束时 FU-A 仍缺少结束片；
12. FU-A 内 SSRC、timestamp、Payload Type 或 NAL Header 变化；
13. 完整 SPS、PPS 或 Slice NAL 的语法非法；
14. Slice 引用不存在或尚未激活的参数集；
15. IDR NAL 重组失败，以及等待后续完整 IDR 的恢复边界。

每条诊断至少包含：

- `severity`；
- 稳定的 `code`；
- 简短说明；
- NAL、RTP 序列号、SSRC、timestamp 或字节位置等证据；
- 可能影响；
- 可以确定时给出恢复条件。

## 5. 收口顺序

### Milestone A：文档与报告契约

- [x] 冻结 `v0.1.0` 范围和非目标；
- [x] 区分当前能力和目标能力；
- [x] 冻结 CLI 与退出码；
- [x] 定义 `Diagnostic`、`AnalysisReport` 和 JSON 字段；

完成条件：实现过程中不再需要扩大协议范围，文本和 JSON 可以共享同一报告模型。

### Milestone B：RTP 会话分析

- [x] 增加确定性的 RTP 流分析器；
- [x] 维护单一活动 SSRC 和 Payload Type；
- [x] 支持丢包、重复、乱序和序列号回绕；
- [x] 实现 RFC 3550 interarrival jitter；
- [x] 将 RFC 6184 解包和 FU-A 重组接入分析器；
- [x] 将可恢复的 RTP/RFC 6184 解析错误转换为诊断并继续分析；

完成条件：由内存字节和显式到达时间驱动的测试可以复现全部 RTP 诊断，不依赖真实网络。

当前状态：已完成。正常 RTP、参数集缺失后恢复、IDR FU-A 丢片及后续 IDR 恢复均已进入
统一 `AnalysisReport` 测试。

### Milestone C：报告与 CLI

- [x] 实现统一报告模型；
- [x] 实现文本报告；
- [x] 实现 JSON 报告；
- [x] 实现跨平台 UDP 输入；
- [x] 接通 `listen --udp`；
- [x] 增加 `--version` 和稳定退出码；

完成条件：Annex-B 和 UDP 两条入口均能生成语义一致的文本或 JSON 报告。

当前状态：已完成。Windows 本机 UDP datagram 已通过 CLI 进入 RTP/H.264 分析并生成 JSON
报告；平台差异封装在输入适配器中。

### Milestone D：验证与故障注入

- [x] 正常 Annex-B 与 RTP 确定性样本；
- [x] FU-A 丢首片、丢中片和丢尾片；
- [x] 丢包、重复、乱序和序列号回绕；
- [ ] 与 FFmpeg/ffprobe 和 Wireshark 交叉验证；
- [ ] Linux ASan/UBSan（配置已完成，等待 CI 验收）；
- [ ] 至少一个解析器 fuzz target 和固定语料冒烟；
- [ ] 1080p30 RTP 连续监听 30 分钟；

完成条件：故障场景可重复、损坏输入不崩溃、网络异常后分析器可以恢复并继续统计。

### Milestone E：v0.1.0 发布

- [x] 确定项目许可证（MIT）；
- [ ] Windows/MSVC 和 Linux/GCC CI（工作流已完成，等待首次通过）；
- [ ] Windows x64 便携包；
- [x] 正常流与 IDR FU-A 丢片的一分钟演示；
- [ ] GitHub Release、SHA-256 和完整使用说明；

完成条件：新用户下载 Release 后，可以在一分钟内完成一次正常分析和一次故障分析。

## 6. v0.1.0 完成定义

以下条件全部满足后停止开发本版本，转入 SemiLive：

- Annex-B 和 UDP/RTP 两条入口真实可用；
- 正常流可以输出 H.264 与 RTP 摘要；
- IDR 的 FU-A 中间片丢失可以输出证据、可能影响和恢复条件；
- 文本与 JSON 对同一输入给出一致结论；
- 回绕、丢包、乱序、重复、截断输入均有自动化测试；
- CI、Sanitizer、Release 验收全部通过；
- README 只把已经实现的功能描述为当前能力；
- 一分钟演示和故障注入步骤可以由新用户复现。

## 7. 首版之后

仅在 SemiLive 或目标岗位明确需要时再选择：

- PCAP + SDP 离线分析；
- AVCC 输入；
- RTSP 输入适配；
- RTCP、丢包反馈与恢复分析；
- H.265/RTP；
- HTML 可视化报告。
