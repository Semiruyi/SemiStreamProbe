# 外部工具交叉验证

本文记录 `v0.1.0` 与外部工具的可复现对照结果。媒体和运行报告保存在被 Git 忽略的
`samples/local/`，仓库只保留生成方法和结论。

## FFmpeg/ffprobe 8.1.2

验证日期：2026-08-28。工具来自 MSYS2 UCRT64，FFmpeg 构建启用了 libx264。

生成 10 秒 1080p30 Annex-B：

```powershell
C:\msys64\ucrt64\bin\ffmpeg.exe `
  -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 10 `
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 60 -bf 2 `
  -f h264 samples/local/testsrc-1080p30.h264
```

`ffprobe -count_frames` 与 `semistreamprobe inspect` 的结果：

| 字段 | ffprobe | SemiStreamProbe |
|---|---:|---:|
| Profile | High | High |
| Level | 4.0 | 4.0 |
| 分辨率 | 1920×1080 | 1920×1080 |
| 总帧/AU | 300 | 300 |
| I/IDR | 5 | 5 |
| P | 161 | 161 |
| B | 134 | 134 |

Annex-B 报告为 `complete`，错误诊断为 0。

## FFmpeg RTP/H.264

使用同一媒体，通过 FFmpeg RTP muxer 发送到本机监听器：

```powershell
semistreamprobe listen --udp 127.0.0.1:5007 `
  --payload-type 96 --duration 12 --output json

C:\msys64\ucrt64\bin\ffmpeg.exe `
  -framerate 30 -re -i samples/local/testsrc-1080p30.h264 `
  -an -c:v copy -f rtp -payload_type 96 `
  "rtp://127.0.0.1:5007?pkt_size=1200"
```

接收结果：

- RTP 包 `6213/6213`，丢包、重复和乱序均为 0；
- 300 个 Slice、300 个 Access Unit、5 个 IDR；
- High、Level 4.0、1920×1080；
- error 为 0，报告状态为 `complete`。

FFmpeg 每个 IDR 前发送一个 NRI 为 0、但内部最大 NRI 为 3 的 STAP-A，因此产生 5 条
`H264_STAP_A_NRI_MISMATCH` warning。RFC 6184 §5.7 要求聚合包 NRI 必须等于内部 NAL
的最大 NRI；SemiStreamProbe 保留该合规性证据，同时继续分析结构完整的内部 NAL。

## 1080p30 长时监听

验证日期：2026-08-29。FFmpeg 实时生成 1920×1080、30 fps 的 `testsrc2`，使用
libx264 ultrafast、单线程、GOP 60 编码，通过 RTP 发送 1800 秒。SemiStreamProbe 同时
监听 1800 秒，并以 30 秒间隔采样进程工作集。

结果：

- 报告状态 `complete`，监听窗口约 1798.9 秒；
- RTP 包 `2,987,970/2,987,970`，丢包、重复和乱序均为 0；
- 53,981 个 Slice、53,981 个 Access Unit、900 个 IDR；
- Baseline、Level 4.0、1920×1080；
- error 为 0；900 条 warning 均为已知的 `H264_STAP_A_NRI_MISMATCH`；
- SemiStreamProbe 工作集从 7.1 MiB 增至最多 30.2 MiB，随累计 AU 和诊断近似线性增长，
  未出现增长加速或异常退出；FFmpeg 最大工作集为 109.4 MiB。

监听器先于发送器约 1 秒启动，并在发送器结束前到达自身 1800 秒期限，因此最终 AU 比
理论值 54,000 少 19；RTP 序列统计确认监听窗口内没有丢包。

## 后续验证

当前环境未安装 Wireshark/tshark。RTP Header 字段抓包核对推迟到 `v0.1.0` 之后，不阻塞
首版发布；后续可使用同一 RTP 流核对 sequence、timestamp、marker、SSRC 和 Payload Type。
