#include "config/app_config.hpp"

#include <cassert>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

int main()
{
    const auto legacy=load_tracker_config("config/tracking/bytetrack.yaml");
    assert(legacy.type==TrackerType::ByteTrack && !legacy.post_reid);
    const auto post=load_tracker_config("config/tracking/bytetrack_post_reid.yaml");
    assert(post.type==TrackerType::ByteTrack && post.post_reid);
    const auto bot=load_tracker_config("config/tracking/botsort.yaml");
    assert(bot.type==TrackerType::BotSort && bot.kalman_format=="xywh" &&
        bot.appearance_max_crops_per_frame==0);
    const auto runtime=load_runtime_config("config/runtime.yaml");
    assert(runtime.tracking_catalog.size()==3 && runtime.initial_tracker_id==1);
    assert(runtime.tracking_catalog.at(2).config.type==TrackerType::BotSort);
    assert(runtime.tracking_catalog.at(3).config.post_reid);
    bool failed=false;
    try { config_parser::load_tracking_config(YAML::Load(
        "type: botsort\ntrack_threshold: 0.5\nmatch_threshold: 0.8\ntrack_buffer: 30\n")); }
    catch(const std::runtime_error&) { failed=true; }
    assert(failed);
    return 0;
}
