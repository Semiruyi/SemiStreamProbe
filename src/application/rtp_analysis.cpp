#include "semi_stream_probe/application/rtp_analysis.hpp"

#include "semi_stream_probe/core/h264_syntax.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace semi_stream_probe::application {

AnalysisReport make_rtp_analysis_report(
    const H264RtpStreamAnalyzer& analyzer,
    std::string source,
    std::optional<std::uint64_t> duration_us) {
    const auto& rtp = analyzer.rtp_statistics();
    const auto& stream = analyzer.stream_statistics();

    AnalysisReport report;
    report.analysis = {
        .kind = AnalysisKind::rtp_session,
        .status = AnalysisStatus::complete,
    };
    report.input = {
        .kind = InputKind::udp,
        .source = std::move(source),
        .bytes_read = std::nullopt,
        .datagrams_received = rtp.datagrams_received,
        .duration_us = duration_us,
    };

    report.h264.nal_units = stream.nal_units;
    report.h264.sps = static_cast<std::uint64_t>(
        stream.sequence_parameter_sets.size());
    report.h264.pps = static_cast<std::uint64_t>(
        stream.picture_parameter_sets.size());
    report.h264.slices = stream.slices;
    report.h264.access_units = static_cast<std::uint64_t>(
        stream.access_units.size());
    report.h264.idr_access_units = static_cast<std::uint64_t>(
        stream.gop.idr_access_unit_indices.size());
    report.h264.leading_non_idr_access_units = static_cast<std::uint64_t>(
        stream.gop.leading_non_idr_access_units);
    report.h264.slice_types = {
        .i = static_cast<std::uint64_t>(stream.gop.kinds.i),
        .p = static_cast<std::uint64_t>(stream.gop.kinds.p),
        .b = static_cast<std::uint64_t>(stream.gop.kinds.b),
        .sp = static_cast<std::uint64_t>(stream.gop.kinds.sp),
        .si = static_cast<std::uint64_t>(stream.gop.kinds.si),
        .mixed = static_cast<std::uint64_t>(stream.gop.kinds.mixed),
    };
    if (stream.gop.average_idr_interval) {
        report.h264.idr_interval_au = IdrIntervalReport{
            .average = *stream.gop.average_idr_interval,
            .minimum = static_cast<std::uint64_t>(
                *stream.gop.minimum_idr_interval),
            .maximum = static_cast<std::uint64_t>(
                *stream.gop.maximum_idr_interval),
        };
    }

    if (!stream.sequence_parameter_sets.empty()) {
        const auto& sps = stream.sequence_parameter_sets.front();
        report.h264.profile = h264_profile_name(sps.profile_idc);
        report.h264.level = h264_level_name(sps);
        report.h264.resolution = ResolutionReport{
            .width = sps.width,
            .height = sps.height,
        };
    }
    report.h264.sequence_parameter_sets.reserve(
        stream.sequence_parameter_sets.size());
    for (const auto& sps : stream.sequence_parameter_sets) {
        report.h264.sequence_parameter_sets.push_back(
            SequenceParameterSetReport{
                .id = sps.seq_parameter_set_id,
                .profile = h264_profile_name(sps.profile_idc),
                .level = h264_level_name(sps),
                .resolution = {
                    .width = sps.width,
                    .height = sps.height,
                },
            });
    }
    report.h264.picture_parameter_sets.reserve(
        stream.picture_parameter_sets.size());
    for (const auto& pps : stream.picture_parameter_sets) {
        report.h264.picture_parameter_sets.push_back(
            PictureParameterSetReport{
                .id = pps.pic_parameter_set_id,
                .sequence_parameter_set_id = pps.seq_parameter_set_id,
            });
    }

    report.rtp = RtpReport{
        .ssrc = rtp.ssrc,
        .payload_type = rtp.payload_type,
        .clock_rate_hz = rtp.clock_rate_hz,
        .first_sequence_number = rtp.first_sequence_number,
        .last_sequence_number = rtp.last_sequence_number,
        .packets_received = rtp.packets_received,
        .unique_packets_received = rtp.unique_packets_received,
        .packets_expected = rtp.packets_expected,
        .packets_lost = rtp.packets_lost,
        .duplicate_packets = rtp.duplicate_packets,
        .out_of_order_packets = rtp.out_of_order_packets,
        .jitter = std::nullopt,
    };
    if (rtp.jitter_timestamp_units && rtp.clock_rate_hz != 0) {
        report.rtp->jitter = RtpJitterReport{
            .timestamp_units = *rtp.jitter_timestamp_units,
            .milliseconds = *rtp.jitter_timestamp_units * 1000.0 /
                            static_cast<double>(rtp.clock_rate_hz),
        };
    }

    const auto diagnostics = analyzer.diagnostics();
    report.diagnostics.assign(diagnostics.begin(), diagnostics.end());
    const bool has_error = std::ranges::any_of(
        report.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
    if (has_error || rtp.out_of_order_packets != 0) {
        report.analysis.status = AnalysisStatus::partial;
    }
    return report;
}

} // namespace semi_stream_probe::application
