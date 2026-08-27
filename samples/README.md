# 测试样本与故障注入

`semistreamprobe_demo` 随项目一起构建，只使用 C++ 标准库和系统 UDP socket。它生成固定的
Baseline 4.0、1920×1080 SPS/PPS、一个 IDR Slice Header 和一个 P Slice Header，适合验证
SemiStreamProbe 的语法、RTP 和诊断链路。

这些确定性 NAL 只包含本项目解析范围内的 Slice Header，不包含完整宏块数据，因此不是
用于播放或解码质量评价的媒体。真实媒体交叉验证见本文最后一节。

## 1. Annex-B 正常样本

```powershell
build\mingw-debug\semistreamprobe_demo.exe annex-b `
  --output samples\local\synthetic.h264

build\mingw-debug\semistreamprobe.exe inspect `
  samples\local\synthetic.h264 --output json
```

预期摘要：

- `resolution = 1920x1080`；
- `nal_units = 4`；
- `access_units = 2`；
- `diagnostic_summary.error = 0`。

## 2. RTP 正常流

先在终端 A 启动监听：

```powershell
build\mingw-debug\semistreamprobe.exe listen `
  --udp 127.0.0.1:5004 --payload-type 96 --duration 5 --output json
```

再在终端 B 发送正常 FU-A：

```powershell
build\mingw-debug\semistreamprobe_demo.exe send `
  --target 127.0.0.1:5004 --scenario normal
```

预期收到 6 个 datagram，恢复 4 个 NAL 和 2 个 Access Unit，丢包及错误诊断均为 0。

## 3. IDR FU-A 丢中片与恢复

终端 A 使用相同监听命令，终端 B 改为：

```powershell
build\mingw-debug\semistreamprobe_demo.exe send `
  --target 127.0.0.1:5004 --scenario fu-middle-loss
```

发送器保留 RTP 序列号 `1003` 但不发送该 datagram，随后发送 FU-A end 和一个完整恢复 IDR。
报告应为 `partial`，`packets_lost = 1`，并按顺序包含：

```text
RTP_SEQUENCE_GAP
H264_FU_A_SEQUENCE_GAP
H264_IDR_INCOMPLETE
```

后续完整 IDR 仍应进入 H.264 模型，`idr_access_units = 1`。

## 4. FU-A 缺首片和缺尾片

```powershell
# continuation/end 在没有 start 的情况下到达
build\mingw-debug\semistreamprobe_demo.exe send `
  --target 127.0.0.1:5004 --scenario fu-missing-start

# 只发送 start/middle，由监听结束触发 incomplete 诊断
build\mingw-debug\semistreamprobe_demo.exe send `
  --target 127.0.0.1:5004 --scenario fu-missing-end
```

缺首片场景产生 `H264_FU_A_MISSING_START`；缺尾片场景产生
`H264_FU_A_INCOMPLETE` 和 `H264_IDR_INCOMPLETE`。

## 5. 真实媒体与外部工具交叉验证

`samples/local/` 被 Git 忽略，用于保存自动生成或尚未确认再分发权利的媒体。安装 FFmpeg
后，可以生成一段具有明确来源的测试图并编码为 Annex-B：

```powershell
ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 10 `
  -c:v libx264 -pix_fmt yuv420p -g 60 -bf 2 `
  -f h264 samples/local/testsrc-1080p30.h264

ffprobe -v error -show_entries stream=codec_name,profile,level,width,height `
  -of json samples/local/testsrc-1080p30.h264

build\mingw-debug\semistreamprobe.exe inspect `
  samples\local\testsrc-1080p30.h264 --output json
```

通过 RTP 发送同一媒体：

```powershell
ffmpeg -re -stream_loop -1 -i samples/local/testsrc-1080p30.h264 `
  -an -c:v copy -f rtp "rtp://127.0.0.1:5004?pkt_size=1200"
```

Wireshark/tshark 只用于交叉核对 RTP sequence、timestamp、marker、SSRC 和 Payload Type；
它不作为核心分析器的运行时依赖。执行交叉验证时应在 `samples/local/` 保存命令输出或
抓包证据，不提交来源不明确的大型二进制媒体。
