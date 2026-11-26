#pragma once

#include "config/app_config.hpp"
#include "common/track.hpp"
#include "gui/gui_protocol.hpp"
#include <sys/socket.h>
#include <utility>

class FrameTelemetryCollector;

// Owns the metadata UDP destination, socket and wire encoding.
class GuiMetadataSender {
public:
    explicit GuiMetadataSender(GuiMetadataConfig config);
    ~GuiMetadataSender();
    GuiMetadataSender(const GuiMetadataSender&) = delete;
    GuiMetadataSender& operator=(const GuiMetadataSender&) = delete;

    bool send(const TrackingResult& result, std::uint64_t gui_started_us,
        std::uint32_t timestamp, FrameTelemetryCollector* telemetry);
    void close();

private:
    bool open_metadata_socket();
    GuiMetadataConfig metadata_config_;
    PcGuiProtocol protocol_;
    int metadata_fd_ = -1;
    sockaddr_storage metadata_address_{};
    socklen_t metadata_address_size_ = 0;
};
