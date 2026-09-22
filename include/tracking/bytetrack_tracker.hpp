#pragma once

#include <string>
#include <vector>

#include "tracking/tracker.hpp"


// ByteTrack 알고리즘을 이용해 Detection을 Track으로 연결한다.
class ByteTrackTracker : public Tracker
{
private:
    // ByteTrack 설정 YAML 경로
    std::string config_path_;

public:
    explicit ByteTrackTracker(
        std::string config_path
    );

    ~ByteTrackTracker() override = default;

    // Detection 목록을 이용해 ByteTrack Tracking을 수행한다.
    std::vector<Track> track(
        const std::vector<Detection>& detections
    ) override;

    // ByteTrack 내부 상태를 초기화한다.
    void reset() override;
};
