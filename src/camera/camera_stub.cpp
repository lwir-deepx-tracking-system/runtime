#include "camera/camera.hpp"

#include "common/logger.hpp"


Camera::~Camera() = default;

bool Camera::open()
{
    Logger::error(
        "[Camera] i3system SDK가 비활성화되어 있습니다 "
        "(LWIR_ENABLE_I3_CAMERA=OFF)");
    return false;
}

bool Camera::capture(FrameContext&)
{
    return false;
}

void Camera::close()
{
}
