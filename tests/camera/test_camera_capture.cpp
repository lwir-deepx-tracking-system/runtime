#include <chrono>

#include <opencv2/core.hpp>

#include "camera/camera.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "pipeline/camera_thread.hpp"


// 실제 Thermal Expert에서 CameraThread queue로 첫 프레임이 전달되는지 확인한다.
int main()
{
    Camera camera;
    ThreadSafeQueue<FrameMessage> output_queue;
    CameraThread camera_thread(camera, output_queue, false);

    camera_thread.start();

    FrameMessage message;
    const bool received = output_queue.pop(message);

    // 첫 프레임을 받은 뒤 producer가 다음 push에서 종료되도록 한다.
    output_queue.close();
    camera_thread.join();

    if (!received) return 1;
    if (!message.frame) return 2;
    if (message.frame->image.empty()) return 3;
    if (message.frame->image.type() != CV_16UC1) return 4;
    if (message.frame->image.cols != 640 || message.frame->image.rows != 480) return 5;
    if (!message.frame->image.isContinuous()) return 6;
    if (message.frame->metadata.frame_id != 1) return 7;
    if (message.frame->metadata.captured_at ==
        std::chrono::steady_clock::time_point{}) return 8;

    return 0;
}
