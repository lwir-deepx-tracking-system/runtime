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
- 기존 Runtime의 yolov8n/yolo26s와 repvgg YAML은 보존한다. 새 모델 YAML은 전처리·출력 계약 구현과 검증 뒤 등록한다. 현재 새 실행 가능 카탈로그 등록은 하지 않았다.
- 호스트 bank에서 SHA 일치를 확인한 모델: NEWMRW480, NEWMRT480A/B, RVGGA0_REID2C256, S480, S6, M640. 추가 RepVGG/ViT-lite는 원격에서 확인했다.
- `/home/orangepi/workspace/model-bank/final_tracker/tracker/models/NEWMRT480A.dxnn` 및 B 링크 대상은 실제 파일 이름과 달라 broken link다. 원본은 변경하지 않았다.
- 원본 run_real_gui.sh의 절대 실행 경로는 현재 장비에서 없다. 실행 중 Python 프로세스의 cwd는 `/home/orangepi/workspace/lwir_model_bank/tracking_server`였다. ZIP과 현재 장비 코드가 같다고 가정하지 않는다.
