#include "tracking/bytetrack_tracker.hpp"

#include <utility>

#include "common/logger.hpp"


ByteTrackTracker::ByteTrackTracker(
    std::string config_path)
    : config_path_(std::move(config_path))
{
    Logger::info(
        "[Tracking] ByteTrackTracker 생성 완료"
    );

    Logger::debug(
        "[Tracking] ByteTrack 설정 파일: " +
        config_path_
    );
}


// Detection 목록을 이용해 ByteTrack Tracking을 수행한다.
std::vector<Track> ByteTrackTracker::track(
    const std::vector<Detection>& detections)
{
    std::vector<Track> tracks;

    (void)detections;

    // TODO:
    // 1. Detection Confidence 기준 분류
    // 2. 기존 Track Prediction
    // 3. Detection과 Track Association
    // 4. Track 상태 및 ID 갱신
    // 5. Track 목록 생성

    return tracks;
}


// ByteTrack이 유지하는 내부 Tracking 상태를 초기화한다.
void ByteTrackTracker::reset()
{
    // TODO:
    // Track ID
    // Kalman Filter
    // Association 상태 초기화

    Logger::info(
        "[Tracking] ByteTrackTracker 상태 초기화 완료"
    );
}
