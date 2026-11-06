# 공용 애플리케이션 지원

`lwir_app_support`는 `include/app`의 인터페이스와 `src/app`의 구현을 제공한다.
GUI, 카메라, NPU 또는 특정 애플리케이션 설정 구조체에 의존하지 않는다.

- `PipelineConsole`: 비차단 입력, 명령 파싱, 비동기 완료 출력 담당.
  호출자가 `IPipelineRequests`를 구현하고 실행 루프에서 `poll()`을 호출한다.
  `poll()`이 false를 반환하면 저장 종료를 요청한다. EOF는 종료 명령이 아니다.
- `MetricsWriter`: 실행 폴더, 설정 사본, 세대별 모델 연결, 모델별 비동기 CSV 저장 담당.
  호출자는 YAML을 해석한 뒤 `MetricsWriterOptions`에 저장 루트, 모델별 설정 경로,
  추가 설정 사본의 원본 경로와 상대 목적지를 전달한다.
  모델 전환 시 flush하고 같은 모델에 돌아오면 기존 파일에 이어 쓴다.
- `app_shutdown::SignalScope`: SIGINT/SIGTERM의 저장 종료 요청과 SIGQUIT의 폐기 종료
  요청을 등록하고, 범위 종료 시 이전 신호 처리기를 복원한다. 프로세스당 하나를
  최상위 실행 범위에서 작업 스레드 시작 전에 생성한다.
  신호 처리기는 플래그만 변경한다. 호출자가 작업 스레드를 정지·합류하고,
  저장 시 `close()`, 폐기 시 `discard()`를 호출한다.
- `create_run_directory`: 타임스탬프와 충돌 회피 접미사로 실행 폴더를 생성한다.
  런타임 측정과 벤치마크가 같은 구현을 사용한다.

런타임은 위 모듈을 조립하고 파이프라인의 수명과 요청 처리 순서를 관리한다.
공용 모듈이 런타임의 작업 스레드나 모델 생성 정책을 소유하지 않는다.
PC 응답의 `GuiPipelineResult`는 공용 `PipelineResult`의 별칭이며 기존 바이트 값을 유지한다.
