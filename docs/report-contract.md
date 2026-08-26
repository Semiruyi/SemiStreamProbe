# SemiStreamProbe 诊断与报告契约

本文冻结 `v0.1.0` 的诊断模型、统一报告模型和 JSON 字段。实现可以调整内部类型和存储
方式，但不得让文本与 JSON 分别判断协议事实，也不得在不更新 `schema_version` 的情况
下改变已有 JSON 字段的含义。

## 1. 模型边界

```text
Parser ──→ ParseError
                │
                ▼
Session/Application policy
                │
                ├── unrecoverable ──→ task failure + exit code 1
                │
                └── recoverable ────→ Diagnostic
                                           │
Analyzer statistics ───────────────────────┤
                                           ▼
                                    AnalysisReport
                                      ├── TextReporter
                                      └── JsonReporter
```

- `ParseError` 描述一个结构为何无法解析，主要面向代码定位；
- `Diagnostic` 描述一次可记录、可解释的流异常，主要面向使用者；
- `AnalysisReport` 是一次任务完成后的唯一事实来源；
- Reporter 只格式化，不重新推断丢包、IDR 影响或恢复条件。

文件无法打开、命令行非法等任务级失败不伪装成流诊断。UDP 会话中的单包解析失败通常
转换为诊断后继续监听；Annex-B 的关键结构无法继续解析时，首版允许任务失败。

## 2. Diagnostic

首版逻辑结构：

```cpp
enum class Severity {
    info,
    warning,
    error,
};

struct DiagnosticLocation {
    std::optional<std::size_t> input_byte_offset;
    std::optional<std::size_t> bit_offset;
    std::optional<std::size_t> nal_index;
    std::optional<std::uint16_t> rtp_sequence_number;
    std::optional<std::uint32_t> ssrc;
    std::optional<std::uint32_t> rtp_timestamp;
};

struct Diagnostic {
    Severity severity;
    DiagnosticCode code;
    std::string summary;
    std::string evidence;
    std::optional<std::string> impact;
    std::optional<std::string> recovery;
    DiagnosticLocation location;
};
```

约束：

- `code` 是稳定的机器接口，使用大写 `UPPER_SNAKE_CASE`；
- `summary` 是不依赖具体数值的简短说明；
- `evidence` 只陈述本次输入中实际观察到的事实；
- `impact` 使用“可能”表达无法由码流证据确定的播放结果；
- `recovery` 只在存在明确协议恢复边界时填写；
- 不适用的位置字段在 JSON 中为 `null`，不使用 `0` 充当缺失值；
- 同一根因可以产生传输诊断和更高层影响诊断，例如序列缺口同时导致 IDR 不完整。

## 3. v0.1.0 诊断码

| Code | 默认级别 | 触发事实 |
|---|---|---|
| `RTP_INVALID_PACKET` | error | RTP Header、extension、padding 或数据长度非法 |
| `RTP_UNEXPECTED_PAYLOAD_TYPE` | warning | Payload Type 与监听配置不一致，包被忽略 |
| `RTP_UNEXPECTED_SSRC` | warning | SSRC 与当前活动流不一致，包被忽略 |
| `RTP_SEQUENCE_GAP` | error | 扩展序列空间中观察到一个或多个缺失包 |
| `RTP_DUPLICATE_PACKET` | warning | 已经接收的序列号再次到达 |
| `RTP_OUT_OF_ORDER_PACKET` | warning | 旧序列号在更新序列号之后到达 |
| `H264_RTP_INVALID_PAYLOAD` | error | RFC 6184 负载类型或字段非法 |
| `H264_STAP_A_INVALID` | error | STAP-A 长度、空 NAL、嵌套类型或 NRI 约束非法 |
| `H264_FU_A_MISSING_START` | error | continuation/end 到达时没有活动起始片 |
| `H264_FU_A_SEQUENCE_GAP` | error | FU-A 重组期间序列号不连续，当前 NAL 被丢弃 |
| `H264_FU_A_INTERRUPTED` | error | 前一个 FU-A 未完成便收到新的起始片 |
| `H264_FU_A_CONTEXT_CHANGED` | error | FU-A 内 SSRC、timestamp、PT、NRI 或 NAL 类型变化 |
| `H264_PARAMETER_SET_NOT_FOUND` | error | Slice 引用不存在或尚未激活的参数集 |
| `H264_IDR_INCOMPLETE` | error | 已确定不完整的 FU-A 承载 IDR NAL |

`RTP_SEQUENCE_GAP` 与 `H264_FU_A_SEQUENCE_GAP` 不重复表达同一层事实：前者描述会话连续性，
后者描述该缺口已经破坏一个正在重组的 NAL。`H264_IDR_INCOMPLETE` 再说明这个 NAL 对随机
接入和画面恢复边界的意义。

默认级别是首版行为。若未来允许用户配置严重程度，JSON 中仍输出本次实际采用的级别。

## 4. AnalysisReport

根对象固定包含以下字段：

| 字段 | 类型 | 含义 |
|---|---|---|
| `schema_version` | string | JSON 契约版本，首版为 `1.0` |
| `generator` | object | 工具名称和程序版本 |
| `analysis` | object | 分析类型与完成状态 |
| `input` | object | 输入种类、来源及读取规模 |
| `h264` | object | H.264 语法和 Access Unit 摘要 |
| `rtp` | object/null | RTP 会话摘要；Annex-B 输入为 `null` |
| `diagnostic_summary` | object | info、warning、error 数量 |
| `diagnostics` | array | 按观察顺序排列的诊断 |

### 4.1 analysis

```json
{
  "kind": "annex_b | rtp_session",
  "status": "complete | partial"
}
```

- `complete`：所有被接受的输入均完成处理，没有因可恢复错误丢弃媒体单元；
- `partial`：报告成功生成，但至少一个包、NAL 或其他媒体单元因诊断被忽略或丢弃；
- 任务级失败不产生一个伪造的 `failed` 报告，而是写标准错误并返回退出码 `1`。

发现 warning 不必自动把状态变为 `partial`。例如重复包可以被忽略且不破坏已完成的 NAL。

### 4.2 input

```json
{
  "kind": "file | udp",
  "source": "sample.h264 | udp://0.0.0.0:5004",
  "bytes_read": 20000,
  "datagrams_received": null,
  "duration_us": null
}
```

- 不适用或未测量的数值为 `null`；
- `duration_us` 对 UDP 表示从首个被接收 datagram 到监听结束的单调时钟时长；
- 不输出操作系统单调时钟的绝对值；
- 文件路径按用户传入形式记录，不主动展开用户目录或解析真实路径。

### 4.3 h264

```json
{
  "codec": "H.264/AVC",
  "profile": "High",
  "level": "4.2",
  "resolution": { "width": 1920, "height": 1080 },
  "nal_units": 605,
  "sps": 1,
  "pps": 1,
  "slices": 600,
  "access_units": 600,
  "idr_access_units": 5,
  "slice_types": {
    "i": 5,
    "p": 595,
    "b": 0,
    "sp": 0,
    "si": 0,
    "mixed": 0
  },
  "idr_interval_au": {
    "average": 120.0,
    "minimum": 120,
    "maximum": 120
  },
  "nal_list": null
}
```

- 尚未解析到有效 SPS 时，`profile`、`level` 和 `resolution` 为 `null`；
- 少于两个完整 IDR Access Unit 时，`idr_interval_au` 为 `null`；
- `nal_list` 仅在 `inspect --nal-list` 时为数组，否则为 `null`；
- RTP 中被确认不完整并丢弃的 NAL 不计入 `nal_units`，其影响通过诊断和 RTP 统计表达；
- 所有计数均为非负 JSON integer，累计实现使用至少 64 位无符号整数。

`nal_list` 中每项固定包含：

```json
{
  "index": 0,
  "byte_offset": 0,
  "size": 28,
  "type": "SPS",
  "nal_ref_idc": 3,
  "access_unit": null,
  "slice_type": null,
  "frame_num": null
}
```

### 4.4 rtp

```json
{
  "ssrc": 287454020,
  "payload_type": 96,
  "clock_rate_hz": 90000,
  "first_sequence_number": 1000,
  "last_sequence_number": 2199,
  "packets_received": 1200,
  "unique_packets_received": 1200,
  "packets_expected": 1200,
  "packets_lost": 0,
  "duplicate_packets": 0,
  "out_of_order_packets": 0,
  "jitter": {
    "timestamp_units": 72.0,
    "milliseconds": 0.8
  }
}
```

- `ssrc` 和 RTP timestamp 使用 JSON integer，文本报告可以额外显示十六进制；
- 首包尚未建立有效会话时，序列号、SSRC 和 jitter 为 `null`；
- `packets_expected` 使用扩展序列号空间计算；
- `packets_lost = max(packets_expected - unique_packets_received, 0)`；
- `packets_received` 包含通过 RTP 结构校验且属于活动 SSRC/PT 的 datagram，包含重复包；
- `unique_packets_received` 排除重复包，但包含确认属于活动序列空间的乱序包；
- `duplicate_packets` 和 `out_of_order_packets` 分开统计，同一包只归入最具体的一类；
- 已经见过的序列号再次到达属于 duplicate；未见过但落后于当前最高扩展序列号属于
  out-of-order；
- jitter 按 RFC 3550 计算，`milliseconds = timestamp_units * 1000 / clock_rate_hz`。

### 4.5 diagnostics

```json
{
  "severity": "error",
  "code": "H264_FU_A_SEQUENCE_GAP",
  "summary": "FU-A sequence is discontinuous",
  "evidence": "expected RTP sequence 12030, received 12031",
  "impact": "the current NAL unit cannot be reconstructed safely",
  "recovery": "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
  "location": {
    "input_byte_offset": null,
    "bit_offset": null,
    "nal_index": null,
    "rtp_sequence_number": 12031,
    "ssrc": 287454020,
    "rtp_timestamp": 90000
  }
}
```

`diagnostic_summary` 必须与 `diagnostics` 数组按实际 severity 重新计数后的结果一致。

## 5. JSON 兼容规则

- JSON 使用 UTF-8；
- 根对象字段按本文顺序输出，测试不依赖对象字段顺序；
- 数组保持分析顺序；
- 固定模型字段缺失时输出 `null`，不省略；
- 不输出 `NaN`、`Infinity` 或字符串形式的数字；
- 浮点值使用 JSON number，golden tests 比较解析后的值而不是小数文本格式；
- `schema_version` 采用 `major.minor`；
- 删除字段、改名或改变含义需要增加 major；
- 只新增可选字段可以增加 minor；
- `v0.x` 程序版本可以继续输出 `schema_version: 1.0`，两者独立演进。

首版 JSON 的完整示例：

- [Annex-B 文件报告](examples/report-annex-b.json)；
- [正常 RTP 会话报告](examples/report-rtp.json)；
- [FU-A IDR 丢片报告](examples/report-fu-a-loss.json)。

## 6. 文本报告约束

文本输出面向人工阅读，不承诺逐字符兼容，但固定按以下区段排列：

```text
Input
H.264
RTP             # 仅 RTP 会话
Diagnostics
NAL list        # 仅 inspect --nal-list
```

约束：

- 数值单位必须显式出现；
- 同一诊断展示 code、证据、可能影响和恢复条件；
- 没有诊断时明确输出 `Diagnostics: 0 errors, 0 warnings`；
- 文本中的所有结论都能在同一 `AnalysisReport` 的 JSON 中找到对应字段；
- 自动化消费者应使用 JSON，不解析文本表格。

## 7. 退出码与报告的关系

| 退出码 | 含义 | 报告 |
|---|---|---|
| `0` | 分析任务完成 | 正常输出，允许包含 warning/error diagnostics |
| `1` | 输入、I/O 或运行时失败 | 不保证产生报告，错误写入标准错误 |
| `2` | 命令行用法错误 | 不产生分析报告，usage 写入标准错误 |

流质量诊断不是进程执行失败。首版不增加 `--fail-on-diagnostic`；需要质量门禁的调用方读取
JSON 中的 `diagnostic_summary`。
