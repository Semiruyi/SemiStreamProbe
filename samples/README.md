# 本地测试样本

`samples/local/` 会被 Git 忽略，用于保存尚未确认再分发权利的本地媒体。

使用以下命令可以从 H.264 MP4 中提取一段 Annex-B 样本，过程不会重新编码：

```powershell
ffmpeg -ss 0 -i input.mp4 -t 10 -map 0:v:0 -an -c:v copy `
  -bsf:v h264_mp4toannexb samples/local/sample.h264
```

将二进制样本加入版本控制前，必须记录素材来源、生成命令和再分发许可证。
