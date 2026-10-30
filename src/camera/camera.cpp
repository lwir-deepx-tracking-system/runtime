#include "camera/camera.hpp"

#include <chrono>
#include <string>
#include <thread>

#include "common/logger.hpp"

#include <i3system_TE.h>
#include <i3_types.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

Camera::~Camera()
{
  close();
}

// Camera를 초기화한다.
bool Camera::open()
{

  // STEP 1: Scan for connected devices until one is available.
  // -----------------------------------------------------------------
  int          targetDevIndex = -1;
  int          targetProdVer  = -1;
  unsigned int targetCoreID   = 0;
  std::string  modelName      = "Unknown";

  while(targetDevIndex < 0)
  {
    Logger::info("[Camera] 연결된 카메라 스캔");

    i3::SCANINFO scanDevs[MAX_USB_NUM];
    int scanResult = ScanTE(scanDevs);

    if(scanResult == 1)
    {
      for(int i = 0; i < MAX_USB_NUM; ++i)
      {
        if(scanDevs[i].bDevCon == 1)
        {
          targetDevIndex = i;
          targetProdVer  = scanDevs[i].nProdVer;
          targetCoreID   = scanDevs[i].nCoreID;

          switch(scanDevs[i].nProdVer)
          {
            case I3_TE_EQ1:
              modelName = "EQ1 (Type A, QVGA)";
              break;
            case I3_TE_EV1:
              modelName = "EV1 (Type A, VGA)";
              break;
            case I3_TE_EQ2:
              modelName = "EQ2 (Type A, QVGA)";
              break;
            case I3_TE_EV2:
              modelName = "EV2 (Type A, VGA)";
              break;
            default:
              modelName = "Type B or Other";
              break;
          }

          Logger::info("[Camera] Found Device Index [" + std::to_string(i) + "]: " + modelName + " | Core Serial: " + std::to_string(targetCoreID) + "\n");
          break; // Select first detected device
        }
      }
    }

    if(targetDevIndex < 0)
    {
      Logger::info("[Camera] 카메라를 찾지 못했습니다. 1초 후 다시 스캔합니다.");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  // STEP 2: Connect to the Camera
  // -----------------------------------------------------------------
  Logger::info("[Camera] TE-" + modelName + " 열기 시도");

  // TE-EV1 열기.
  te_ = i3::OpenTE_A(targetDevIndex);

  if(!te_)
  {
    Logger::error("[Camera] TE-" + modelName + " 열기 실패");
    return false;
  }

  width_  = te_->GetImageWidth();
  height_ = te_->GetImageHeight();
  if(width_ <= 0 || height_ <= 0)
  {
    Logger::error("[Camera] 유효하지 않은 영상 크기");
    close();
    return false;
  }

  te_->SetShutterMode(I3_TEMP_SHUTTER);
  te_->SetShutterTemperature(1);

  te_->ShutterCalibrationOn();

  return true;
}

bool Camera::capture(FrameContext &frame)
{
  if(!te_ || width_ <= 0 || height_ <= 0)
    return false;

  // SDK가 애플리케이션 소유 버퍼에 직접 기록하므로 FrameContext가 영상
  // 수명을 소유한다. 실제 장치 크기로 할당해 버퍼 오버런을 방지한다.
  frame.image.create(height_, width_, CV_16UC1);
  auto *buffer = reinterpret_cast<unsigned short *>(frame.image.data);

  // 학습/전처리의 고정 16-bit 입력 범위를 유지하기 위해 SDK AGC를 끈다.
  const int receive_result = te_->RecvImage(buffer, true);
  // Logger::info("[Camera] Frame 결과 " + std::to_string(receive_result));

  if(receive_result != 1)
    return false;

  return true;
}

// Camera 자원을 해제한다.
void Camera::close()
{
  if(!te_)
    return;

  te_->CloseTE();
  te_     = nullptr;
  width_  = 0;
  height_ = 0;
  Logger::info("[Camera] 닫기 완료");
}
