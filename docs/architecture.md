# SemiStreamProbe 架构骨架

当前版本已接通 Annex-B 文件读取、NAL 边界识别、SPS/PPS、完整基础 Slice Header、
Access Unit 组装和逐 NAL 文本输出。后续解析逻辑继续按学习顺序逐个模块补充，
并通过测试样本验证。

## 1. 目标

SemiStreamProbe 是一个同步、确定性、无第三方运行时依赖的 H.264/RTP 码流诊断工具。
首个阶段只关注：

- Annex-B 文件输入；
- NAL Unit 边界识别；
- NAL Header 解析；
- Slice Header 与 Access Unit 组装；
- 后续逐步增加 GOP 和 RTP 能力。

它不承担播放器职责，也不依赖 SemiPlayer 的运行时模块。

## 2. 依赖方向

```text
CLI
 ↓
Application
 ↓
Core
```

- `CLI`：命令行参数、退出码和最终输出；
- `Application`：一次检查任务的编排、文件输入和报告选择；
- `Core`：纯码流处理，不能依赖 CLI、操作系统 API 或 FFmpeg。

Core 内部的目标数据流为：

```text
Input bytes
  ↓
Framing: Annex-B / future RTP depacketizer
  ↓
Syntax: NAL / RBSP / SPS / PPS / Slice
  ↓
Model: parameter sets / access units / GOP
  ↓
Diagnostics and reports
```

RTP 解包器未来仍然输出 NAL Unit，因此文件输入和网络输入可以复用同一套语法解析器。

## 3. 当前目录职责

```text
include/semi_stream_probe/core/
  types.hpp          基础字节类型和 ByteView
  parse_error.hpp    结构化解析错误
  annex_b.hpp        Annex-B 扫描器接口
  nal.hpp            NAL Header 接口
  rbsp.hpp           EBSP 到 RBSP 转换接口
  bit_reader.hpp     位读取与 Exp-Golomb 接口
  access_unit.hpp    编码图像边界判断与 Access Unit 组装接口
  gop.hpp            AU Slice 类型分类与 IDR 分隔的 GOP 统计接口
  h264_syntax.hpp    SPS/PPS 数据模型与解析接口
  parameter_sets.hpp SPS/PPS 注册与 ID 查询
  slice.hpp          基础 Slice Header、参考管理和预测控制解析接口

include/semi_stream_probe/application/
  inspect.hpp        检查任务和报告编排接口

src/core/
  Annex-B、NAL、RBSP、位读取、SPS、PPS、参数集注册、Slice、Access Unit 与 GOP 统计已实现

src/application/
  文件读取、核心解析编排和文本输出

src/cli/
  可执行程序入口和命令行外壳

tests/
  无第三方测试框架的核心语法、应用层和编译冒烟测试
```

## 4. 数据所有权

第一版的 Annex-B 扫描器返回 `NalUnitRef`，它只保存输入缓冲区中的偏移和长度，不拥有
数据。调用方负责保证原始字节缓冲区的生命周期。

解析器接收 `ByteView`，只返回值类型结果。报告层在需要跨越输入生命周期保存内容时，
再复制必要的摘要字段，而不是让报告保存悬空视图。

RTP FU-A 重组以后，重组器会拥有自己的 NAL 缓冲区，但仍以 `ByteView` 交给语法解析器。

## 5. 错误边界

解析函数使用 `std::expected<T, ParseError>` 表示“无法产生结果”。`ParseError` 保留：

- 错误码；
- 字节偏移；
- 比特偏移；
- NAL 索引；
- 面向用户的简短信息。

未来的批量检查还会增加 `Diagnostic`，用于记录可以跳过并继续分析的警告或错误。解析
错误和诊断报告分开，避免把所有异常都变成异常退出。

## 6. 跨平台策略

Core 只使用 C++ 标准库中的固定宽度整数、`std::span`、`std::vector`、`std::expected`
和基础字符串类型，不使用平台 API。

文件读取暂时也使用标准 C++ 文件流。未来 UDP 输入如果需要平台适配，只允许出现在
Input/Infrastructure 边界，不向 Core 泄漏 Windows socket 或 POSIX socket 类型。

CMake 是构建工具，不属于程序运行时依赖。当前测试不引入 GoogleTest 等外部测试库，
后续如果测试规模明显增长，再单独评估是否引入测试依赖。

## 7. 实现顺序

1. Annex-B 三字节/四字节 Start Code；
2. NAL Header；
3. EBSP 到 RBSP；
4. BitReader 和 Exp-Golomb；
5. SPS/PPS；
6. Slice Header 和 Access Unit；
7. GOP/统计；
8. RTP Header、Single NAL、STAP-A、FU-A；
9. 诊断、JSON 和故障注入。

当前已完成上述第 1 至 6 步（Slice 不含宏块数据），并通过 CLI 接通
SPS/PPS/Slice/Access Unit 摘要、GOP 统计和逐 NAL 输出。下一阶段将实现
参数集变化诊断，然后进入 RTP/H.264。

