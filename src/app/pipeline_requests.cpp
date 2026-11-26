#include "app/pipeline_requests.hpp"

PipelineCatalogItem IPipelineRequests::model_info(std::uint32_t) const { return {}; }

PipelineCatalogItem IPipelineRequests::tracker_info(std::uint32_t) const { return {}; }

PipelineTrackerPolicy IPipelineRequests::tracker_policy() const { return {}; }

PipelineAutoDetection IPipelineRequests::auto_detection() const { return {}; }

bool IPipelineRequests::set_auto_detection(bool) { return false; }

bool IPipelineRequests::set_auto_detection_thresholds(float,float) { return false; }

NpuFeedState IPipelineRequests::npu_feed_state() const { return {}; }

bool IPipelineRequests::set_npu_feed(std::uint8_t) { return false; }

bool IPipelineRequests::request_camera_reopen() { return false; }

bool IPipelineRequests::recording_available() const { return false; }

PipelineRecordingStatus IPipelineRequests::recording_status() const { return {}; }

bool IPipelineRequests::start_recording(const std::string&) { return false; }

bool IPipelineRequests::stop_recording() { return false; }

std::string IPipelineRequests::latest_recording() const { return {}; }

std::vector<std::string> IPipelineRequests::list_recordings() const { return {}; }

PipelineRecordingFileChunk IPipelineRequests::read_recording_file(
    const std::string&, std::uint8_t, std::uint64_t, std::uint16_t) const { return {}; }

PipelineChangeTicket IPipelineRequests::request_policy_change(const TrackerPolicyUpdate&) {
    return {PipelineResult::NotReady, {}};
}
