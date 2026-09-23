# 모델 artifact

`yolov8n.dxnn` 같은 컴파일된 모델은 Git에 포함하지 않습니다. 모델 registry 또는
승인된 배포 위치에서 받은 파일을 이 디렉터리에 배치합니다.

배포 시에는 DXNN 파일과 함께 다음 정보를 기록해야 합니다.

- SHA-256 해시
- 학습 및 export 버전
- 전처리 계약과 대응하는 모델 YAML 버전
- DX compiler 버전
- 호환 DX-RT 버전

현재 설정이 기대하는 기본 경로는 `models/yolov8n.dxnn`입니다.
