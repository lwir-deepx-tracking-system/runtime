#include "common/frame_telemetry_collector.hpp"
#include "gui/gui_protocol.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* why)
{ if (!condition) throw std::runtime_error(why); }
std::vector<std::uint8_t> fixture()
{
    std::ifstream file(std::string(PROJECT_ROOT_DIR) + "/tests/fixtures/runtime-telemetry.hex");
    require(bool(file), "PC byte fixture must exist");
    std::string word, text;
    while (file >> word) text += word;
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return bytes;
}
void wire()
{
    const auto expected = fixture();
    GuiTrackingMetadata packet;
    require(decode_tracking_metadata(expected.data(), expected.size(), packet), "decode PC fixture");
    require(packet.frame_id == 32 && packet.telemetry && packet.telemetry->frame_id == 16 &&
        packet.telemetry->session_id == 0x0102030405060708ULL && packet.telemetry->tracker_epoch == 1 &&
        packet.telemetry->sample_age_ms == 1.5F, "fixture identity");
    for (unsigned i = 0; i < kTelemetryMetricCount; ++i)
        require(packet.telemetry->values[i] == float(i), "float32 endian and slot order");
    std::vector<std::uint8_t> encoded;
    require(encode_tracking_metadata(packet, 1200, encoded) && encoded == expected, "exact PC bytes");
    for (std::size_t n = 0; n < expected.size(); ++n) {
        if (n == kGuiMetadataHeaderSize) continue; // Valid legacy packet with zero tracks.
        GuiTrackingMetadata output;
        require(!decode_tracking_metadata(expected.data(), n, output), "reject truncated trailer");
    }
    for (unsigned offset : {28U, 32U, 60U, 64U, 68U, 72U, 76U, 80U}) {
        auto corrupt = expected;
        // Invalid magic / zero required identity / reserved mask / NaN age/value.
        if (offset == 28) corrupt[offset] = 0;
        else if (offset == 32) for (unsigned j = 0; j < 8; ++j) corrupt[offset + j] = 0;
        else if (offset == 60) for (unsigned j = 0; j < 4; ++j) corrupt[offset + j] = 0xff;
        else if (offset == 64) corrupt[offset] = 0x80; // reserved mask bit
        else if (offset == 68 || offset == 80) {
            corrupt[offset] = 0x7f; corrupt[offset+1] = 0xc0;
        } else for (unsigned j = 0; j < 4; ++j) corrupt[offset + j] = 0;
        GuiTrackingMetadata output;
        require(!decode_tracking_metadata(corrupt.data(), corrupt.size(), output), "reject invalid trailer");
    }
    auto extra = expected; extra.push_back(0);
    require(!decode_tracking_metadata(extra.data(), extra.size(), packet), "reject extra byte");
    require(decode_tracking_metadata(expected.data(), expected.size(), packet), "restore fixture");
    packet.telemetry->valid_mask &= ~(1U << 5);
    packet.telemetry->values[5] = std::numeric_limits<float>::quiet_NaN();
    require(encode_tracking_metadata(packet, 1200, encoded), "unmeasured slot ignored");
    require(encoded[28+52+20] == 0 && encoded[28+52+23] == 0, "invalid slot serializes zero");
    GuiTrackMetadata track; track.track_id = 1; track.width = track.height = 20; track.confidence = .8F;
    packet.tracks.assign(42, track); packet.tracks.back().track_id = 999;
    packet.selected_track_id = 999;
    require(encode_tracking_metadata(packet, 1200, encoded) && encoded.size() == 1177,
        "reserve 124B and cap at 41 tracks");
    GuiTrackingMetadata decoded;
    require(decode_tracking_metadata(encoded.data(), encoded.size(), decoded) &&
        decoded.tracks.size() == 41 && decoded.tracks.front().track_id == 999, "selected target priority");
    packet.telemetry.reset(); packet.telemetry_enabled = true;
    require(encode_tracking_metadata(packet, 1200, encoded) && encoded.size() == 1053,
        "reserve capacity even before first telemetry");
    require(!encode_tracking_metadata(packet, 151, encoded), "reject insufficient reserved space");
    packet.telemetry_enabled = false;
    require(encode_tracking_metadata(packet, 1200, encoded) && encoded.size() == 1078,
        "telemetry off preserves legacy capacity");
}
using Collector = FrameTelemetryCollector;
using Clock = Collector::Clock;
struct Csv : IMeasurementSink {
    int activates = 0, stages = 0, details = 0;
    void activate(std::uint64_t, int) noexcept override { ++activates; }
    void record(std::uint64_t, MetricStage, const StageMetric&) noexcept override { ++stages; }
    void record_detection(std::uint64_t, const DetectionTiming&) noexcept override { ++details; }
};
void stages(Collector& c, std::uint64_t frame, std::uint64_t epoch = 0, std::uint64_t generation = 1)
{
    StageMetric stage; stage.frame_id = frame; stage.processing_ms = 0;
    stage.queue_wait_ms = 2; stage.e2e_ms = 12;
    c.record(generation, MetricStage::Camera, stage);
    c.record(generation, MetricStage::Detection, stage);
    c.bind_tracking(generation, frame, epoch, 2, Clock::now() - std::chrono::milliseconds(50));
    c.record(generation, MetricStage::Tracking, stage);
    DetectionTiming timing; timing.frame_id = frame;
    timing.inference_started_at = Clock::now(); timing.inference_finished_at = timing.inference_started_at;
    c.record_detection(generation, timing);
}
void begin(Collector& c, std::uint64_t frame, std::uint32_t rtp,
    std::uint64_t epoch = 0, std::uint64_t generation = 1)
{
    const auto now = Clock::now();
    c.gui_begin(generation, frame, epoch, now - std::chrono::milliseconds(50), rtp, 2, 3, now);
}
void done(Collector& c, std::uint32_t rtp, bool early_marker = false)
{
    if (early_marker) c.rtp_ready(rtp);
    c.gui_submitted(rtp, 4, true);
    if (!early_marker) c.rtp_ready(rtp);
}
void collection()
{
    Csv csv;
    Collector c(&csv); c.activate(1, 7); c.start_session();
    stages(c, 8); begin(c, 8, 0xfffffff0U); done(c, 0xfffffff0U, true);
    auto sample = c.latest_before(10, 1, 0);
    require(sample && sample->frame_id == 8 && sample->model_id == 7 && sample->tracker_id == 2,
        "collect exact frame identities");
    require((sample->valid_mask & (1U << 0)) && sample->values[0] == 0,
        "valid measured zero is retained");
    require(!(sample->valid_mask & ((1U << 5) | (1U << 10) | (1U << 3))),
        "missing profiler/control/timing remain invalid");
    require(sample->valid_mask & (1U << 4), "zero duration inference timestamp is valid");
    require((sample->valid_mask & (1U << 17)) && sample->values[17] >= 0, "capture to RTP measured");
    const auto original = *sample;
    StageMetric late; late.frame_id = 8; late.processing_ms = 99;
    c.record(1, MetricStage::Control, late);
    sample = c.latest_before(10, 1, 0);
    require(sample && sample->valid_mask == original.valid_mask, "freeze late optional values");
    require(!c.latest_before(8, 1, 0), "never attach same/current frame");
    stages(c, 9); begin(c, 9, 2); // Wrap is matched by identity, never timestamp ordering.
    require(c.latest_before(10, 1, 0)->frame_id == 8, "skip incomplete N-1");
    done(c, 2);
    require(c.latest_before(11, 1, 0)->frame_id == 9, "accept wrapped RTP timestamp");
    stages(c, 7); begin(c, 7, 3); done(c, 3);
    require(c.latest_before(11, 1, 0)->frame_id == 9, "late older completion cannot regress newest");
    stages(c, 10, 1); begin(c, 10, 4, 1); done(c, 4);
    sample = c.latest_before(12, 1, 1);
    require(sample && sample->tracker_epoch == 1 && !c.latest_before(12, 1, 0), "tracker epoch isolation");
    const auto session = sample->session_id;
    c.start_session();
    require(!c.latest_before(12, 1, 1), "session restart clears old sample");
    stages(c, 11, 1); begin(c, 11, 4, 1); done(c, 4, true);
    sample = c.latest_before(12, 1, 1);
    require(sample && sample->session_id != session, "new sender session identity");
    require(!c.latest_before(20, 1, 1, Clock::now() + std::chrono::seconds(3)), "TTL expires telemetry");
    require(c.expired_frames() > 0 && csv.activates == 1 && csv.stages >= 10 && csv.details >= 4,
        "CSV fanout remains active");
    c.activate(2, 8);
    stages(c, 12, 1, 1); // Late prior generation events must be ignored.
    require(!c.latest_before(20, 2, 1), "model generation transition invalidates prior data");
    stages(c, 13, 1, 2); begin(c, 13, 0, 1, 2); done(c, 0);
    require(c.latest_before(20, 2, 1)->model_id == 8, "new model attribution");
    stages(c, 14, 1, 2); begin(c, 14, 1, 1, 2);
    c.rtp_ready(1); c.gui_submitted(1, 4, false);
    require(c.latest_before(20, 2, 1)->frame_id == 13, "failed appsrc push never completes a sample");
    c.reset_context();
    c.rtp_ready(0);
    require(!c.latest_before(20, 2, 1), "bypass clears old RTP mappings");
    for (std::size_t i = 1; i <= Collector::EventCapacity + 5; ++i) {
        StageMetric s; s.frame_id = i; c.record(2, MetricStage::Camera, s);
    }
    require(c.dropped_events() >= 5, "bounded event queue reports loss");
    c.latest_before(10000, 2, 1);
    require(c.expired_frames() > 100, "bounded incomplete frame capacity");
    // Activation survives saturation, so subsequent models cannot retain old identity.
    for (std::size_t i = 0; i < Collector::EventCapacity; ++i) c.rtp_ready(0);
    c.activate(3, 9);
    require(!c.latest_before(10000, 3, 1), "activation retained under queue saturation");
}
}
int main()
{
    try { wire(); collection(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
