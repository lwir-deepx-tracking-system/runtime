#include "reid/reid_matcher.hpp"

#include <stdexcept>
#include <unordered_map>

namespace {
Track person(int id, float x = 0.0F)
{
    Track track;
    track.track_id = id;
    track.class_id = 0;
    track.x = x;
    track.y = 0.0F;
    track.width = 20.0F;
    track.height = 40.0F;
    return track;
}
}

int main()
{
    ReidMatcherConfig config;
    config.similarity_threshold = 0.8F;
    ReidMatcher matcher(config, 2);
    if (!matcher.needs_decision_feature(10)) return 1;
    auto first = matcher.step(1, {person(10)}, {{10, {1.0F, 0.0F}}});
    if (first.tracks[0].track_id != 10 || !first.matches.empty() ||
        matcher.needs_decision_feature(10)) return 2;
    matcher.step(2, {}, {});
    auto relinked = matcher.step(3, {person(20, 5.0F)}, {{20, {1.0F, 0.0F}}});
    if (relinked.tracks[0].track_id != 10 || relinked.matches.size() != 1 ||
        relinked.matches[0].tracker_id != 20 || relinked.matches[0].stable_id != 10)
        return 3;
    auto continued = matcher.step(4, {person(20, 6.0F)}, {});
    if (continued.tracks[0].track_id != 10) return 4;

    // 원본 ID 10이 다시 보이더라도 이미 사용 중인 stable ID 10과 중복하지 않는다.
    auto returned = matcher.step(5, {person(20), person(10)}, {});
    if (returned.tracks[0].track_id != 10 ||
        returned.tracks[1].track_id == 10 ||
        returned.tracks[1].track_id <= 0) return 5;

    matcher.reset();
    matcher.step(1, {person(1)}, {{1, {1.0F, 0.0F}}});
    matcher.step(2, {}, {});
    auto different = matcher.step(3, {person(2)}, {{2, {0.0F, 1.0F}}});
    if (different.tracks[0].track_id != 2 || !different.matches.empty()) return 6;

    matcher.reset();
    matcher.step(1, {person(1)}, {{1, {1.0F, 0.0F}}});
    matcher.step(2, {}, {});
    auto far = matcher.step(3, {person(2, 1000.0F)}, {{2, {1.0F, 0.0F}}});
    if (far.tracks[0].track_id != 2 || !far.matches.empty()) return 7;

    // 한 개의 사라진 ID는 같은 프레임의 두 새 Track에 중복 할당되지 않는다.
    matcher.reset();
    matcher.step(1, {person(1)}, {{1, {1.0F, 0.0F}}});
    matcher.step(2, {}, {});
    const auto one_to_one = matcher.step(3,
        {person(2, 1.0F), person(3, 2.0F)},
        {{2, {1.0F, 0.0F}}, {3, {1.0F, 0.0F}}});
    if (one_to_one.matches.size() != 1 ||
        one_to_one.tracks[0].track_id == one_to_one.tracks[1].track_id)
        return 9;

    // 새 ID의 첫 프레임에 crop이 없더라도 짧은 대기 기간 안에는 비교한다.
    matcher.reset();
    matcher.step(1, {person(1)}, {{1, {1.0F, 0.0F}}});
    matcher.step(2, {}, {});
    matcher.step(3, {person(2)}, {});
    if (!matcher.needs_decision_feature(2)) return 10;
    const auto delayed = matcher.step(4, {person(2)}, {{2, {1.0F, 0.0F}}});
    if (delayed.tracks[0].track_id != 1 || delayed.matches.size() != 1)
        return 11;

    try
    {
        matcher.step(4, {}, {});
        return 8;
    }
    catch (const std::invalid_argument&) {}
    return 0;
}
