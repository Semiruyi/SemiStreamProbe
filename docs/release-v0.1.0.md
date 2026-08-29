# SemiStreamProbe v0.1.0

`v0.1.0` 是 SemiStreamProbe 的首个可发布版本。它提供两条完整诊断入口：H.264 Annex-B
文件分析，以及单路 UDP/RTP/H.264 实时监听。两条入口共享同一个 H.264 流模型、诊断模型
和文本/JSON 报告模型。

## 主要能力

- 解析 Annex-B、NAL Header、EBSP/RBSP、SPS、PPS 和基础 Slice Header；
- 组装 Access Unit，统计 I/P/B/SP/SI、IDR 和 GOP 间隔；
- 解析 RTP v2、CSRC、extension 和 padding；
- 支持 RFC 6184 Single NAL Unit、STAP-A 和 FU-A；
- 统计 RTP 丢包、重复、乱序、16 位序列号回绕和 RFC 3550 jitter；
- 诊断 FU-A 缺首片、丢中片、缺尾片、上下文变化和恢复边界；
- 输出共享语义的文本或 JSON 报告；
- 提供确定性的正常流与故障注入工具 `semistreamprobe_demo`。

## 下载与校验

下载以下两个文件：

```text
SemiStreamProbe-0.1.0-windows-x64.zip
SemiStreamProbe-0.1.0-windows-x64.zip.sha256
```

在 PowerShell 中校验：

```powershell
$zip = '.\SemiStreamProbe-0.1.0-windows-x64.zip'
$actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
$expected = ((Get-Content -Raw "$zip.sha256") -split '\s+')[0].ToLower()
if ($actual -ne $expected) { throw 'SHA-256 mismatch' }
"SHA-256 verified: $actual"
```

## 一分钟正常分析

解压 ZIP，在解压后的 `SemiStreamProbe-0.1.0-windows-x64` 目录执行：

```powershell
.\bin\semistreamprobe.exe --version
.\bin\semistreamprobe_demo.exe annex-b --output synthetic.h264
.\bin\semistreamprobe.exe inspect synthetic.h264 --output json
```

预期结果包含：

```text
resolution = 1920x1080
nal_units = 4
access_units = 2
diagnostic_summary.error = 0
```

## 一分钟故障分析

终端 A：

```powershell
.\bin\semistreamprobe.exe listen --udp 127.0.0.1:5004 `
  --payload-type 96 --duration 5 --output text
```

终端 B：

```powershell
.\bin\semistreamprobe_demo.exe send --target 127.0.0.1:5004 `
  --scenario fu-middle-loss
```

预期报告包含 `packets_lost = 1`，以及以下诊断：

```text
RTP_SEQUENCE_GAP
H264_FU_A_SEQUENCE_GAP
H264_IDR_INCOMPLETE
```

随后到达的完整 IDR 仍会被分析，证明分析器能越过损坏边界继续工作。

## 验证记录

- Windows/MSVC Release 和 Linux/GCC ASan+UBSan CI 通过；
- 22 个自动化测试通过；
- 与 FFmpeg/ffprobe 8.1.2 的 Annex-B 帧统计和 RTP/H.264 接收结果完成交叉验证；
- 1080p30 RTP 连续监听 30 分钟；
- 监听窗口内接收 `2,987,970/2,987,970` 个 RTP 包，丢包、重复、乱序和 error 均为 0；
- 固定语料 fuzz 冒烟在 Windows/Linux CI 中运行。

详细证据见 [外部工具交叉验证](https://github.com/Semiruyi/SemiStreamProbe/blob/v0.1.0/docs/validation.md)。

## 已知边界

- 只支持 H.264 Annex-B 文件和单路 UDP/RTP/H.264；
- RTP 会话只跟踪一个活动 SSRC 和一个 Payload Type；
- RFC 6184 首版只支持 Single NAL Unit、STAP-A 和 FU-A；
- 乱序会被诊断，但首版不提供通用重排缓冲；
- 不支持 PCAP、SDP、AVCC、RTSP、RTCP、H.265、解码和渲染；
- FFmpeg 8.1.2 发送的部分 STAP-A 外层 NRI 为 0，而内部最大 NRI 大于 0；工具会输出
  `H264_STAP_A_NRI_MISMATCH` warning，同时保留结构完整的内部 NAL 继续分析。

## 许可证

SemiStreamProbe 使用 MIT License。测试媒体、FFmpeg、标准文档和其他第三方工具仍受各自
许可证约束。
