#pragma once

#include "semi_stream_probe/core/diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace semi_stream_probe::application {

enum class AnalysisKind {
    annex_b,
    rtp_session,
};

enum class AnalysisStatus {
    complete,
    partial,
};

enum class InputKind {
    file,
    udp,
};

struct GeneratorReport {
    std::string name{"SemiStreamProbe"};
    std::string version{"0.1.0"};
};

struct AnalysisInfo {
    AnalysisKind kind{AnalysisKind::annex_b};
    AnalysisStatus status{AnalysisStatus::complete};
};

struct InputReport {
    InputKind kind{InputKind::file};
    std::string source;
    std::optional<std::uint64_t> bytes_read;
    std::optional<std::uint64_t> datagrams_received;
    std::optional<std::uint64_t> duration_us;
};

struct ResolutionReport {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct SequenceParameterSetReport {
    std::uint32_t id{0};
    std::string profile;
    std::string level;
    ResolutionReport resolution;
};

struct PictureParameterSetReport {
    std::uint32_t id{0};
    std::uint32_t sequence_parameter_set_id{0};
};

struct SliceTypeReport {
    std::uint64_t i{0};
    std::uint64_t p{0};
    std::uint64_t b{0};
    std::uint64_t sp{0};
    std::uint64_t si{0};
    std::uint64_t mixed{0};
};

struct IdrIntervalReport {
    double average{0.0};
    std::uint64_t minimum{0};
    std::uint64_t maximum{0};
};

struct NalUnitDetailReport {
    std::uint64_t index{0};
    std::uint64_t byte_offset{0};
    std::uint64_t size{0};
    std::string type;
    std::uint8_t nal_ref_idc{0};
    std::optional<std::uint64_t> access_unit;
    std::optional<std::string> slice_type;
    std::optional<std::uint64_t> frame_num;
};

struct H264Report {
    std::string codec{"H.264/AVC"};
    std::optional<std::string> profile;
    std::optional<std::string> level;
    std::optional<ResolutionReport> resolution;
    std::uint64_t nal_units{0};
    std::uint64_t sps{0};
    std::uint64_t pps{0};
    std::uint64_t slices{0};
    std::uint64_t access_units{0};
    std::uint64_t idr_access_units{0};
    std::uint64_t leading_non_idr_access_units{0};
    SliceTypeReport slice_types;
    std::optional<IdrIntervalReport> idr_interval_au;
    std::vector<SequenceParameterSetReport> sequence_parameter_sets;
    std::vector<PictureParameterSetReport> picture_parameter_sets;
    std::optional<std::vector<NalUnitDetailReport>> nal_list;
};

struct RtpJitterReport {
    double timestamp_units{0.0};
    double milliseconds{0.0};
};

struct RtpReport {
    std::optional<std::uint32_t> ssrc;
    std::uint8_t payload_type{0};
    std::uint32_t clock_rate_hz{90'000};
    std::optional<std::uint16_t> first_sequence_number;
    std::optional<std::uint16_t> last_sequence_number;
    std::uint64_t packets_received{0};
    std::uint64_t unique_packets_received{0};
    std::uint64_t packets_expected{0};
    std::uint64_t packets_lost{0};
    std::uint64_t duplicate_packets{0};
    std::uint64_t out_of_order_packets{0};
    std::optional<RtpJitterReport> jitter;
};

struct DiagnosticSummary {
    std::uint64_t info{0};
    std::uint64_t warning{0};
    std::uint64_t error{0};
};

struct AnalysisReport {
    std::string schema_version{"1.0"};
    GeneratorReport generator;
    AnalysisInfo analysis;
    InputReport input;
    H264Report h264;
    std::optional<RtpReport> rtp;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::string_view to_string(AnalysisKind kind) noexcept;
[[nodiscard]] std::string_view to_string(AnalysisStatus status) noexcept;
[[nodiscard]] std::string_view to_string(InputKind kind) noexcept;

[[nodiscard]] DiagnosticSummary
summarize_diagnostics(std::span<const Diagnostic> diagnostics) noexcept;

[[nodiscard]] std::string render_text(const AnalysisReport& report);
[[nodiscard]] std::string render_json(const AnalysisReport& report);

} // namespace semi_stream_probe::application
