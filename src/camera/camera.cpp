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


bool Camera::read(FrameContext& frame)
{
    // i3system SDK에서 raw frame 획득

    // SDK buffer
    //     ↓
    // 640 x 480 x 2 byte
    //     ↓
    // frame.image가 SDK의 재사용 버퍼를 가리키지 않도록 소유 가능한 cv::Mat으로 복사한다.
    // 그래야 shared_ptr<FrameContext>가 GUI 송신 완료까지 영상 수명을 보장할 수 있다.


    // 실제 실행전이니까 false (카메라 연결하고 실행할때는 true로 돌리면 됨요!!!)
    return false;
}


// Camera 자원을 해제한다.
void Camera::close()
{
    // TODO: 실제 Camera 자원 해제

    Logger::info("[Camera] 닫기 완료");
}
