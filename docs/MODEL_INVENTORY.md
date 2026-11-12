# DXNN 모델 조사 (2026-11-11)

실제 파일의 SHA-256과 ZIP Manifest를 비교한 결과다. `parse_model -v`는 파일 메타데이터 검사이며 InferenceEngine 로딩이나 추론 성공을 의미하지 않는다. 전체 원문은 [메타데이터 증거](model_evidence/remote_metadata_20261111.json)에 있다.

| Model | 실제 Orange Pi 경로 | 입력 | 출력 | SHA | DXRT 로딩 |
|---|---|---|---|---|---|
| NEWMRT480A | `/home/orangepi/workspace/model-bank/final_tracker/dxnn/yolo26m_flir_rt480_a_nms_480x384.dxnn` | [1, 480, 384, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| NEWMRT480B | `/home/orangepi/workspace/model-bank/final_tracker/dxnn/yolo26m_flir_rt480_b_nms_480x384.dxnn` | [1, 480, 384, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| RVGGA0_REID2C256 | `/home/orangepi/workspace/model-bank/final_tracker/dxnn/repvgg_tmot_camel_reid.dxnn` | [1, 128, 64, 3] uint8 | `embedding [1,128] float32` | 일치 | 2026-11-11 `seq_004` 사후 ReID/BoT-SORT 추론 성공 |
| NEWMRW480 | `/home/orangepi/workspace/model-bank/final_tracker/dxnn/yolo26m_flir_whole480_nms_640x480.dxnn` | [1, 480, 640, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| S640 | 조사한 bank에서 일치 파일 미확인 | 미확인 | 미확인 | 미확인 | 미실행 |
| S6 | `/home/orangepi/workspace/model-bank/yolo26/yolo26s_nms_640x640.dxnn` | [1, 640, 640, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| M640 | `/home/orangepi/workspace/model-bank/yolo26/yolo26m_flir_nms_640x640.dxnn` | [1, 640, 640, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| S480 | `/home/orangepi/workspace/model-bank/yolo26/yolo26s_nms_640x480.dxnn` | [1, 480, 640, 3] uint8 | box_s8/16/32: [1,4,H/s,W/s]; cls_s8/16/32: [1,16,H/s,W/s]; float32 | 일치 | 미실행 |
| FT1 | 조사한 bank에서 일치 파일 미확인 | 미확인 | 미확인 | 미확인 | 미실행 |
| FT9 | 조사한 bank에서 일치 파일 미확인 | 미확인 | 미확인 | 미확인 | 미실행 |
| REPVGG_DISTILL_INSIDE_20261110 | `/home/orangepi/workspace/lwir_model_bank/tracking_server/models/reid/repvgg_distill_inside_20261110_reid.dxnn` | [1, 128, 64, 3] uint8 | `embedding [1,128] float32` | 일치 | 미실행 |
| REPVGG_FT_INSIDE_20261110 | `/home/orangepi/workspace/lwir_model_bank/tracking_server/models/reid/repvgg_ft_inside_20261110_reid.dxnn` | [1, 128, 64, 3] uint8 | `embedding [1,128] float32` | 일치 | 미실행 |
| VIT_LITE_FT_INSIDE_20261111 | `/home/orangepi/workspace/lwir_model_bank/tracking_server/models/reid/vit_lite_ft_inside_20261111_reid.dxnn` | [1, 128, 64, 3] uint8 | `embedding [1,128] float32`; NPU 경계 /head/Conv_output_0 [1,128,1,1], CPU reshape | 일치 | 미실행 |
| VIT_LITE_DISTILL_INSIDE_20261111 | `/home/orangepi/workspace/lwir_model_bank/tracking_server/models/reid/vit_lite_distill_inside_20261111_reid.dxnn` | [1, 128, 64, 3] uint8 | `embedding [1,128] float32`; NPU 경계 /head/Conv_output_0 [1,128,1,1], CPU reshape | 일치 | 미실행 |

2026-11-12 재확인: 위 표의 기존 보드 경로와 Runtime YAML의
`../model-bank/final_tracker/tracker/models/RVGGA0_REID2C256.dxnn`은 현재
Orange Pi에서 존재하지 않는다. 동일 SHA-256
`dc19d7862a77ffa1d7c7f71f84283e86c33267d9e741b59070b839cb1921ff53`의
파일은 `/home/orangepi/workspace/model-bank/tracking_server/models/reid/repvgg_tmot_camel_reid.dxnn`에 있다.
임시 YAML로 Runtime ID 4 생성·카메라 처리까지 확인했으나 실제 ReID
Embedding 추론 발생 여부는 별도로 확인해야 한다.

## 계약과 경로의 제한

2026-11-11 Orange Pi 기존 `feat/runtime-telemetry` Benchmark에서
`/home/orangepi/workspace/runtime/models/yolo26s_finetuned.dxnn`이
`seq_004` 281프레임에 대해 DXRT NPU 추론 및 출력 기록까지 성공했다.
호스트 통합 커밋 `6f582ea`의 별도 worktree에서도 이 Detection 모델과 위
RepVGG 모델을 사용한 4개 구성의 실제 추론이 완료됐다.
이 검증은 위 Python 모델 목록이나 호스트 개발 브랜치의 모델 등록 검증을
대신하지 않는다. 산출물은 [`TEST_RESULTS.md`](TEST_RESULTS.md)에 기록했다.

- PRE_IM2COL/PRE_FORMATTER는 장치 입력 처리 메타데이터다. 논리 입력은 NHWC uint8이며 실제 SDK 반환 경계는 `use_ort` 설정으로 재검증해야 한다.
- NEWMRT480A/B는 각각 384x480 좌우 tile 모델이다. Python은 640x512 canvas의 rows 16:496, columns 0:384 및 256:640을 합치고 중첩 구간과 좌표를 처리한다. 단일 whole-frame detector로 등록하면 안 된다.
- NEWMRW480/S480은 640x480 원본 대역을 사용한다. S6/M640은 640x640 padding을 사용한다. Python의 live-map 기본값은 smoothed percentile이며 기존 C++ fixed clip/letterbox와 같지 않다.
- Detection의 six-head decode는 stride 8/16/32, regression 4값, 16 class logits에 sigmoid, host NMS다. 클래스 label은 Python과 C++ 모델별로 확인해야 한다.
- RepVGG는 resize 64x128, gray 3채널, uint8 입력과 L2 embedding을 사용한다. ViT-lite는 CPU subgraph를 포함하므로 use_ort=false에서 RepVGG output name을 그대로 쓰면 안 된다.
- 기존 Runtime의 yolov8n/yolo26s YAML은 보존한다. 아래 2026-11-12 절에 새 YAML 등록 현황과 아직 남은 실기 검증을 기록한다.
- 호스트 bank에서 SHA 일치를 확인한 모델: NEWMRW480, NEWMRT480A/B, RVGGA0_REID2C256, S480, S6, M640. 추가 RepVGG/ViT-lite는 원격에서 확인했다.
- `/home/orangepi/workspace/model-bank/final_tracker/tracker/models/NEWMRT480A.dxnn` 및 B 링크 대상은 실제 파일 이름과 달라 broken link다. 원본은 변경하지 않았다.
- 원본 run_real_gui.sh의 절대 실행 경로는 현재 장비에서 없다. 실행 중 Python 프로세스의 cwd는 `/home/orangepi/workspace/lwir_model_bank/tracking_server`였다. ZIP과 현재 장비 코드가 같다고 가정하지 않는다.

## 2026-11-12 현재 model-bank 복사본

- 현재 보드 원본은 `/home/orangepi/workspace/model-bank/tracking_server/models/`다.
  `MANIFEST.json`의 `_files` 18개와 `_links` 9개 SHA-256을 검사했다.
- 호스트 Runtime `models/imported/`와 보드 Runtime
  `/home/orangepi/workspace/runtime/models/imported/`에 18개 DXNN(약 219 MB)을
  복사했다. 원본은 보존했고 이 디렉터리는 Git 제외 대상으로 관리한다.
- 현재 보드의 18개 모두 `parse_model -v`가 성공했다. YOLO26 split-head
  8개는 `images [1,H,640,3] uint8`와 stride 8/16/32의
  `box [1,4,H/s,640/s]`, `cls [1,16,H/s,640/s]` float32를 확인했다.
  NMS-free 1개는 `output0 [1,300,6]` float32다.
- 위 9개 YOLO26 파일에 별도 Detection YAML과 Runtime catalog ID 3..11을
  추가했다. 기본 Detection ID 1은 유지했다. 기존 RepVGG YAML의 경로는
  동일 SHA 파일의 Runtime 복사본으로 수정했다.
- **미검증:** 새 9개 Detection 모델의 DXRT 로딩·추론, Python의
  percentile/EMA 영상 매핑과 현재 C++ fixed mapping의 결과 차이,
  NMS-free 실기 후처리. 다른 RepVGG/ViT-lite 및 YOLOv8/YOLOX 파일은
  복사됐지만 전환 카탈로그에는 아직 등록하지 않았다.

현재 Python 페이지의 기본/자동 등록 YOLO26 파일을 Runtime에 다음처럼 연결했다.
아래 경로는 호스트와 보드의 **Runtime 루트** 기준이다. SHA는 복사 원본
Manifest와 일치하며, `parse_model` 출력은 확인했지만 DXRT 로딩은 아직
실행하지 않았다.

| Runtime ID | Python 모델 또는 파일 | DXNN path (`models/imported/` 아래) | Input | Output | DXRT |
| ---: | --- | --- | --- | --- | --- |
| 3 | NEWMRW480 | `detect/yolo26m/yolo26m_flir_nms_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 4 | S480 | `detect/yolo26s/yolo26s_nms_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 5 | M640 | `detect/yolo26m/yolo26m_flir_nms_640x640.dxnn` | 640×640×3 uint8 | 6 split heads | 미실행 |
| 6 | S6 | `detect/yolo26s/yolo26s_nms_640x640.dxnn` | 640×640×3 uint8 | 6 split heads | 미실행 |
| 7 | inside 1742 | `detect/yolo26m/yolo26m_flir_inside_20261110_1742_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 8 | inside 2020 | `detect/yolo26m/yolo26m_flir_inside_20261110_2020_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 9 | inside patch | `detect/yolo26m/yolo26m_flir_inside_20261111_patch_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 10 | outside patch | `detect/yolo26m/yolo26m_flir_outside_20261110_patch_640x480.dxnn` | 480×640×3 uint8 | 6 split heads | 미실행 |
| 11 | YOLO26s NMS-free | `detect/yolo26s/yolo26s_nmsfree_npu_640x640.dxnn` | 640×640×3 uint8 | `[1,300,6]` float32 | 미실행 |

ReID 5개는 모두 복사됐고 `images [1,128,64,3] uint8`, 최종 출력
`embedding [1,128] float32`를 `parse_model`에서 확인했다. 현재 Runtime
YAML/Tracker Factory는 `reid/repvgg_tmot_camel_reid.dxnn` 한 개만 선택하며
나머지 4개는 GUI 전환·DXRT `use_ort=false` 계약 검증이 필요하다.
