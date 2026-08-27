#include "semi_stream_probe/application/rtp_analysis.hpp"
#include "semi_stream_probe/core/diagnostic.hpp"
#include "semi_stream_probe/core/h264_rtp_stream_analyzer.hpp"
#include "semi_stream_probe/core/types.hpp"

#include "h264_test_data.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

semi_stream_probe::ByteBuffer make_nal(
    semi_stream_probe::Byte header,
    semi_stream_probe::ByteView rbsp) {
    semi_stream_probe::ByteBuffer nal{header};
    const auto ebsp = semi_stream_probe::test::rbsp_to_ebsp(rbsp);
    nal.insert(nal.end(), ebsp.begin(), ebsp.end());
    return nal;
}

semi_stream_probe::ByteBuffer make_idr_slice() {
    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0);      // first_mb_in_slice
    writer.write_ue(7);      // I slice, all slices same type
    writer.write_ue(0);      // pic_parameter_set_id
    writer.write_bits(0, 4); // frame_num
    writer.write_ue(0);      // idr_pic_id
    writer.write_bits(0, 4); // pic_order_cnt_lsb
    writer.write_bit(false); // no_output_of_prior_pics_flag
    writer.write_bit(false); // long_term_reference_flag
    writer.write_se(0);      // slice_qp_delta
    writer.write_ue(1);      // disable_deblocking_filter_idc
    return writer.finish_rbsp();
}

semi_stream_probe::ByteBuffer make_p_slice(std::uint32_t frame_num,
                                           std::uint32_t poc_lsb) {
    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0); // first_mb_in_slice
    writer.write_ue(0); // P slice
    writer.write_ue(0); // pic_parameter_set_id
    writer.write_bits(frame_num, 4);
    writer.write_bits(poc_lsb, 4);
    writer.write_bit(false); // num_ref_idx_active_override_flag
    writer.write_bit(false); // ref_pic_list_modification_flag_l0
    writer.write_bit(false); // adaptive_ref_pic_marking_mode_flag
    writer.write_se(0);      // slice_qp_delta
    writer.write_ue(1);      // disable_deblocking_filter_idc
    return writer.finish_rbsp();
}

std::vector<semi_stream_probe::Byte>
make_packet(std::uint16_t sequence_number,
            std::uint32_t timestamp,
            semi_stream_probe::ByteView payload,
            bool marker = false) {
    constexpr std::uint32_t ssrc = 0x11223344U;
    std::vector<semi_stream_probe::Byte> packet{
        0x80,
        static_cast<semi_stream_probe::Byte>(96U | (marker ? 0x80U : 0U)),
        static_cast<semi_stream_probe::Byte>(sequence_number >> 8U),
        static_cast<semi_stream_probe::Byte>(sequence_number & 0xFFU),
        static_cast<semi_stream_probe::Byte>(timestamp >> 24U),
        static_cast<semi_stream_probe::Byte>((timestamp >> 16U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>((timestamp >> 8U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>(timestamp & 0xFFU),
        static_cast<semi_stream_probe::Byte>(ssrc >> 24U),
        static_cast<semi_stream_probe::Byte>((ssrc >> 16U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>((ssrc >> 8U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>(ssrc & 0xFFU),
    };
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

void push_packet(semi_stream_probe::H264RtpStreamAnalyzer& analyzer,
                 std::uint16_t sequence_number,
                 std::uint32_t timestamp,
                 semi_stream_probe::ByteView payload,
                 bool marker = false) {
    static_cast<void>(analyzer.push(
        make_packet(sequence_number, timestamp, payload, marker),
        std::chrono::milliseconds(sequence_number)));
}

std::size_t count_diagnostic(
    const semi_stream_probe::application::AnalysisReport& report,
    semi_stream_probe::DiagnosticCode code) {
    std::size_t count = 0;
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

struct SampleNals {
    semi_stream_probe::ByteBuffer sps;
    semi_stream_probe::ByteBuffer pps;
    semi_stream_probe::ByteBuffer idr;
    semi_stream_probe::ByteBuffer p_slice;
};

SampleNals make_sample_nals() {
    semi_stream_probe::test::BaselineSpsOptions sps_options;
    sps_options.crop_bottom = 4;
    return SampleNals{
        .sps = make_nal(
            0x67,
            semi_stream_probe::test::make_baseline_sps_rbsp(sps_options)),
        .pps = make_nal(
            0x68, semi_stream_probe::test::make_baseline_pps_rbsp()),
        .idr = make_nal(0x65, make_idr_slice()),
        .p_slice = make_nal(0x61, make_p_slice(1, 1)),
    };
}

void test_normal_rtp_report() {
    auto nals = make_sample_nals();
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    push_packet(analyzer, 1, 0, nals.sps);
    push_packet(analyzer, 2, 0, nals.pps);
    push_packet(analyzer, 3, 3'000, nals.idr, true);
    push_packet(analyzer, 4, 6'000, nals.p_slice, true);
    analyzer.finish();

    const auto report =
        semi_stream_probe::application::make_rtp_analysis_report(
            analyzer, "memory://normal-rtp", 100'000);
    check(report.analysis.kind ==
              semi_stream_probe::application::AnalysisKind::rtp_session,
          "RTP report kind");
    check(report.analysis.status ==
              semi_stream_probe::application::AnalysisStatus::complete,
          "normal RTP report is complete");
    check(report.h264.resolution.has_value() &&
              report.h264.resolution->width == 1920 &&
              report.h264.resolution->height == 1080,
          "RTP SPS resolution");
    check(report.h264.sps == 1 && report.h264.pps == 1,
          "RTP parameter-set counts");
    check(report.h264.slices == 2 && report.h264.access_units == 2,
          "RTP Slice and Access Unit counts");
    check(report.h264.idr_access_units == 1,
          "RTP IDR Access Unit count");
    check(report.h264.slice_types.i == 1 && report.h264.slice_types.p == 1,
          "RTP Access Unit kinds");
    check(report.rtp.has_value() && report.rtp->packets_received == 4 &&
              report.rtp->packets_lost == 0,
          "RTP packet statistics in report");
    check(report.diagnostics.empty(), "normal RTP report has no diagnostics");

    const auto json = semi_stream_probe::application::render_json(report);
    check(json.find("\"kind\": \"rtp_session\"") != std::string::npos,
          "RTP report renders JSON kind");
    check(json.find("\"width\": 1920") != std::string::npos,
          "RTP report renders SPS resolution");
}

void test_parameter_set_recovery() {
    auto nals = make_sample_nals();
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    push_packet(analyzer, 10, 0, nals.pps);
    push_packet(analyzer, 11, 0, nals.sps);
    push_packet(analyzer, 12, 3'000, nals.idr, true);
    push_packet(analyzer, 13, 3'000, nals.pps);
    push_packet(analyzer, 14, 6'000, nals.idr, true);
    analyzer.finish();

    const auto report =
        semi_stream_probe::application::make_rtp_analysis_report(
            analyzer, "memory://parameter-recovery");
    check(report.analysis.status ==
              semi_stream_probe::application::AnalysisStatus::partial,
          "parameter-set failure makes report partial");
    check(count_diagnostic(
              report,
              semi_stream_probe::DiagnosticCode::
                  h264_parameter_set_not_found) == 2,
          "missing SPS and PPS references are diagnosed");
    check(report.h264.pps == 1 && report.h264.slices == 1 &&
              report.h264.access_units == 1,
          "later parameter sets allow syntax analysis to recover");
}

void test_fu_a_idr_loss_report() {
    auto nals = make_sample_nals();
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    push_packet(analyzer, 20, 0, nals.sps);
    push_packet(analyzer, 21, 0, nals.pps);

    const semi_stream_probe::ByteView idr_payload(nals.idr.data() + 1,
                                                   nals.idr.size() - 1);
    check(idr_payload.size() >= 3, "IDR sample has enough FU-A payload");
    semi_stream_probe::ByteBuffer fu_start{0x7C, 0x85, idr_payload[0]};
    semi_stream_probe::ByteBuffer fu_end{0x7C, 0x45};
    fu_end.insert(fu_end.end(), idr_payload.begin() + 2, idr_payload.end());
    push_packet(analyzer, 22, 3'000, fu_start);
    push_packet(analyzer, 24, 3'000, fu_end, true);
    push_packet(analyzer, 25, 6'000, nals.idr, true);
    analyzer.finish();

    const auto report =
        semi_stream_probe::application::make_rtp_analysis_report(
            analyzer, "memory://fu-a-loss");
    check(report.analysis.status ==
              semi_stream_probe::application::AnalysisStatus::partial,
          "FU-A loss makes report partial");
    check(report.rtp.has_value() && report.rtp->packets_lost == 1,
          "FU-A report carries RTP loss");
    check(count_diagnostic(
              report,
              semi_stream_probe::DiagnosticCode::rtp_sequence_gap) == 1,
          "FU-A report contains RTP gap");
    check(count_diagnostic(
              report,
              semi_stream_probe::DiagnosticCode::h264_fu_a_sequence_gap) == 1,
          "FU-A report contains reassembly gap");
    check(count_diagnostic(
              report,
              semi_stream_probe::DiagnosticCode::h264_idr_incomplete) == 1,
          "FU-A report contains IDR impact");
    check(report.h264.idr_access_units == 1,
          "later intact IDR enters the H.264 model");
}

} // namespace

int main() {
    test_normal_rtp_report();
    test_parameter_set_recovery();
    test_fu_a_idr_loss_report();

    if (failures != 0) {
        std::cerr << failures << " RTP analysis test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_rtp_analysis_tests: all tests passed\n";
    return 0;
}
