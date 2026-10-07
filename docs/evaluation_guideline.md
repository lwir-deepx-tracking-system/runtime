# Benchmark · Runtime · Evaluation

## 1. 전체 흐름

```text
Windows Capture / Dataset
          │
          ▼
   Dataset Sequences
          │
          ▼
      Benchmark
     /    |     \
Detection Track  Timing
   │       │
   ▼       ▼
 Detection   TrackEval
 Evaluation  Evaluation
     \       /
      \     /
       Metrics
```

```text
Benchmark = 결과 추출
Evaluation = 성능 계산
Runtime = 실제 실시간 시스템 성능 측정
```

---

## 2. Dataset 계약

Benchmark는 촬영 프로그램을 알지 않는다.

```text
/mnt/lwir_data/datasets/
├── seq_001/
│   └── images/
│       ├── 000001.png
│       ├── 000002.png
│       └── ...
├── seq_002/
│   └── images/
└── ...
```

### Benchmark 입력 조건

```text
Format      : PNG
LWIR        : 16-bit / CV_16UC1
Frame ID    : 000001.png부터 시작
Continuity  : 중복 / 누락 불가
Sequence    : 각각 독립 처리
```

```text
seq_001 → tracker.reset()
seq_002 → tracker.reset()
...
```

Evaluation용 annotation은 별도로 추가한다.

```text
seq_001/
├── images/
├── gt/
│   └── gt.txt
└── seqinfo.ini
```

---

## 3. Benchmark

### 역할

```text
Stored Frame
    ↓
Detection
    ↓
Tracking
    ↓
Prediction + Processing Time 저장
```

Benchmark는 **평가 점수를 계산하지 않는다.**

### 설정

```text
config/
├── benchmark.yaml
└── model/
    └── yolov8n.yaml
```

#### benchmark.yaml

```text
Model config
Detection max_inflight
ByteTrack parameters
Dataset root
Output root
```

#### yolov8n.yaml

```text
Model path
LWIR input format
Model input size
Preprocess
Postprocess
Confidence / NMS
Class
```

### 결과 구조

```text
/mnt/lwir_data/results/benchmark/
└── yolov8n_bytetrack/
    └── YYYYMMDD_HHMMSS/
        ├── benchmark.yaml
        ├── yolov8n.yaml
        ├── detections/
        │   ├── seq_001.txt
        │   └── seq_002.txt
        ├── tracks/
        │   ├── seq_001.txt
        │   └── seq_002.txt
        └── timing/
            ├── seq_001.csv
            └── seq_002.csv
```

동일 초 충돌:

```text
20261007_190531
20261007_190531_1
20261007_190531_2
```

실패한 run은 해당 timestamp 디렉터리를 삭제한다.

---

## 4. Benchmark 출력 형식

### Detection

```text
detections/seq_001.txt
```

```text
frame_id,class_id,confidence,x,y,width,height
```

예:

```text
1,0,0.921300,120.4,75.2,42.1,105.8
```

### Tracking

```text
tracks/seq_001.txt
```

MOTChallenge prediction format:

```text
frame,id,x,y,width,height,confidence,-1,-1,-1
```

예:

```text
1,3,120.4,75.2,42.1,105.8,0.921300,-1,-1,-1
```

```text
1 frame = 여러 Track line
1 sequence = 1 txt
```

### Timing

```text
timing/seq_001.csv
```

```text
frame_id,detection_ms,tracking_ms
```

측정 범위:

```text
detection_ms
detect() 시작 ─────────────→ detect() 반환

tracking_ms
track() 시작 ──────────────→ track() 반환
```

제외:

```text
imread
파일 저장
directory 생성
config snapshot
```

---

## 5. Benchmark vs Evaluation

```text
Benchmark
│
├── Detection prediction
├── Tracking prediction
└── Standalone timing
        │
        ▼
Evaluation
│
├── GT와 비교
├── Detection metric
└── TrackEval
```

### Benchmark가 하지 않는 것

```text
HOTA 계산
IDF1 계산
MOTA 계산
mAP 계산
GT 비교
```

---

## 6. TrackEval 연결

### Ground Truth

TrackEval용 GT:

```text
seq_001/
├── gt/
│   └── gt.txt
└── seqinfo.ini
```

GT 기본 형식:

```text
frame,id,x,y,width,height,active,class,visibility
```

Person class:

```text
class = 1
```

### Tracker Prediction

Benchmark가 생성:

```text
tracks/seq_001.txt
```

```text
frame,id,x,y,width,height,confidence,-1,-1,-1
```

```text
Benchmark tracks
       │
       ▼
MOTChallenge format
       │
       ▼
TrackEval
```

Evaluation에서 TrackEval 설정은 현재 결과 구조에 맞게 지정한다.

```text
TRACKER_SUB_FOLDER = tracks
SKIP_SPLIT_FOL     = True
```

Sequence 길이는:

```text
seqinfo.ini
```

또는 Evaluation에서 `SEQ_INFO`로 전달할 수 있다.

---

## 7. 예상 Evaluation 지표

| 영역 | 주요 지표 |
|---|---|
| Detection | mAP@50, Precision, Recall |
| Tracking | HOTA, IDF1, MOTA, MOTP |
| Benchmark 성능 | detection_ms, tracking_ms |
| Runtime 성능 | queue_wait_ms, processing_ms, e2e_ms |

### Tracking 핵심

```text
HOTA → Detection + Association 종합
IDF1 → ID 유지 성능
MOTA → FP / FN / ID Switch 종합
MOTP → Bounding Box 위치 정확도
```

---

## 8. Runtime

### Pipeline

```text
Camera
  ↓
Detection
  ↓
Tracking
  ├────────→ GUI Sender → PC
  │
  └────────→ Control
                ↑
          TargetSelector
                ↑
          GUI Receiver
```

Tracking 결과는 GUI와 Control이 공유한다.

```text
TrackingResultPtr
├── frame
└── tracks
```

---

## 9. Runtime 실행 모드

```text
core
GUI     OFF
Control OFF
```

```text
gui
GUI     ON
Control OFF
```

```text
full
GUI     ON
Control ON
```

지원하지 않는 조합:

```text
GUI OFF + Control ON
```

Control은 GUI에서 선택된 `track_id`를 사용한다.

---

## 10. Runtime 설정

```text
runtime.yaml
```

주요 설정:

```text
measurement
├── enabled
└── output_root

model
└── config

detection
└── max_inflight

tracking
├── track_threshold
├── match_threshold
└── track_buffer

control
└── enabled

gui
├── enabled
├── video
├── metadata
└── command
```

`control.driver`는 사용하지 않는다.

---

## 11. Runtime 결과

```text
/mnt/lwir_data/results/runtime/
└── yolov8n_bytetrack/
    ├── core/
    │   └── YYYYMMDD_HHMMSS/
    ├── gui/
    │   └── YYYYMMDD_HHMMSS/
    └── full/
        └── YYYYMMDD_HHMMSS/
```

각 run:

```text
YYYYMMDD_HHMMSS/
├── runtime.yaml
├── yolov8n.yaml
└── metrics.csv
```

실패한 run은 timestamp 디렉터리를 삭제한다.

---

## 12. Runtime Metrics

```text
frame_id,stage,queue_wait_ms,processing_ms,e2e_ms
```

| Stage | Queue Wait | Processing | E2E |
|---|:---:|:---:|:---:|
| Camera | - | O | - |
| Detection | O | O | - |
| Tracking | O | O | O |
| Control | O | O | O |

### E2E

```text
Tracking E2E
Camera frame 수신 완료 ─────→ Tracking 완료
```

```text
Control E2E
Camera frame 수신 완료 ─────→ Control 완료
```

GUI 자체 latency는 Orange Pi `metrics.csv`에서 계산하지 않는다.

---

## 13. GUI 통신

```text
Orange Pi → PC

Video
H.264 → RTP → UDP

Metadata
frame_id
rtp_timestamp
gui_started_us
track_id
bbox
→ UDP
```

```text
PC → Orange Pi

Selected track_id
→ TCP
→ TargetSelector
→ Control
```

`gui_started_us`:

```text
GuiSenderThread 처리 시작 시점
```

향후 PC에서 GUI 전송 latency 측정에 사용한다.

---

## 14. 성능 분석 관점

```text
Benchmark
= 알고리즘 자체 처리 성능

Runtime core
= 기본 실시간 Pipeline

Runtime gui
= GUI 송신 부하 포함

Runtime full
= GUI + Control 전체 시스템
```

비교:

```text
Benchmark ↔ Runtime
Standalone vs 실제 시스템 부하

core ↔ gui
GUI 활성화 영향

gui ↔ full
Control 추가 영향
```
