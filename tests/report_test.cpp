#include "semi_stream_probe/application/report.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

semi_stream_probe::application::AnalysisReport make_report() {
    using namespace semi_stream_probe;
    using namespace semi_stream_probe::application;

    AnalysisReport report;
    report.input.source = "quoted \" path\\line\n";
    report.input.bytes_read = 42;
    report.h264.nal_units = 1;
    report.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = DiagnosticCode::h264_fu_a_sequence_gap,
        .summary = "FU-A sequence is discontinuous",
        .evidence = "expected 10, received 11",
        .impact = "the NAL is incomplete",
        .recovery = std::nullopt,
        .location = {
            .input_byte_offset = std::nullopt,
            .bit_offset = std::nullopt,
            .nal_index = std::nullopt,
            .rtp_sequence_number = 11,
            .ssrc = 0x11223344U,
            .rtp_timestamp = 90'000U,
        },
    });
    return report;
}

void test_enum_strings_and_summary() {
    using namespace semi_stream_probe;
    using namespace semi_stream_probe::application;

    check(to_string(AnalysisKind::annex_b) == "annex_b",
          "Annex-B analysis kind string");
    check(to_string(AnalysisKind::rtp_session) == "rtp_session",
          "RTP analysis kind string");
    check(to_string(AnalysisStatus::complete) == "complete",
          "complete analysis status string");
    check(to_string(AnalysisStatus::partial) == "partial",
          "partial analysis status string");
    check(to_string(InputKind::file) == "file", "file input kind string");
    check(to_string(InputKind::udp) == "udp", "UDP input kind string");

    const auto report = make_report();
    const auto summary = summarize_diagnostics(report.diagnostics);
    check(summary.info == 0, "diagnostic info count");
    check(summary.warning == 0, "diagnostic warning count");
    check(summary.error == 1, "diagnostic error count");
}

void test_json_contract() {
    const auto json =
        semi_stream_probe::application::render_json(make_report());

    check(json.starts_with("{\n  \"schema_version\": \"1.0\""),
          "JSON starts with schema version");
    check(json.find("\"source\": \"quoted \\\" path\\\\line\\n\"") !=
              std::string::npos,
          "JSON escapes strings");
    check(json.find("\"resolution\": null") != std::string::npos,
          "JSON uses null for unavailable resolution");
    check(json.find("\"rtp\": null") != std::string::npos,
          "Annex-B JSON uses null RTP report");
    check(json.find("\"error\": 1") != std::string::npos,
          "JSON derives diagnostic summary");
    check(json.find("\"code\": \"H264_FU_A_SEQUENCE_GAP\"") !=
              std::string::npos,
          "JSON emits stable diagnostic code");
    check(json.find("\"recovery\": null") != std::string::npos,
          "JSON uses null for unavailable recovery");
    check(json.ends_with("}\n"), "JSON ends with one newline");
}

void test_text_uses_same_report() {
    const auto text =
        semi_stream_probe::application::render_text(make_report());

    check(text.find("NAL units: 1") != std::string::npos,
          "text uses report NAL count");
    check(text.find("Diagnostics: 1 errors, 0 warnings") !=
              std::string::npos,
          "text derives diagnostic summary");
    check(text.find("H264_FU_A_SEQUENCE_GAP") != std::string::npos,
          "text emits the same diagnostic code");
    check(text.find("Recovery:") == std::string::npos,
          "text omits unavailable recovery");
}

} // namespace

int main() {
    test_enum_strings_and_summary();
    test_json_contract();
    test_text_uses_same_report();

    if (failures != 0) {
        std::cerr << failures << " report test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_report_tests: all tests passed\n";
    return 0;
}
