#include "camera/camera.hpp"

#include "common/logger.hpp"


// Camera를 초기화한다.
bool Camera::open()
{
    Logger::info("[Camera] 열기 시도");

    // TODO: 실제 LWIR Camera 초기화

    // 실제 실행전이니까 false (카메라 연결하고 실행할때는 true로 돌리면 됨요!!!)
    return false;
}


bool Camera::read(Frame& frame)
{
    // i3system SDK에서 raw frame 획득

    // SDK buffer
    //     ↓
    // 640 x 480 x 2 byte
    //     ↓
    // frame.image에 연결 또는 복사


    // 실제 실행전이니까 false (카메라 연결하고 실행할때는 true로 돌리면 됨요!!!)
    return false;
}


// Camera 자원을 해제한다.
void Camera::close()
{
    // TODO: 실제 Camera 자원 해제

    Logger::info("[Camera] 닫기 완료");
}
