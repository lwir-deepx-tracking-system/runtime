# Orange Pi 통합 브랜치 Benchmark: `seq_004`

- 호스트 소스 커밋: `6f582eab2d311bdaa02d1f5015965188ace879e9`.
- Orange Pi 기존 체크아웃: `feat/runtime-telemetry`,
  `71eb9c84922723176ca8312a5b87bddff82d2387`.
- 분리 worktree에서 DXRT 활성 CMake 빌드 후 `lwir_benchmark` 실행.
  모든 구성은 같은 `seq_004` 281개 원본 16비트 프레임과
  `models/yolo26s_finetuned.dxnn`을 사용했다.
- ReID 구성은 `RVGGA0_REID2C256.dxnn` 실모델을 사용했다.
  원격 기존 Python 추적 서버가 동시에 NPU를 사용 중이었다.

| 구성 | 종료 | 평균 Detection ms | Tracking ms | ReID ms | 전체 ms | Track 행 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ByteTrack | 0 | 31.695 | 0.215 | 0 | 32.214 | 2709 |
| ByteTrack + 사후 ReID | 0 | 28.968 | 0.245 | 6.252 | 35.866 | 2709 |
| BoT-SORT + 사전 ReID | 0 | 28.692 | 0.281 | 10.193 | 39.448 | 2660 |
| 필터·GMC + 사후 ReID | 0 | 28.372 | 13.938 | 5.657 | 48.366 | 1512 |

프레임별 시간은 각 구성의 `seq_004_timing.csv`에 있다. 네 실행의 Detection
출력 SHA-256은 동일하며 앞 12자리는 `39050215394c`다. 따라서 입력 Detection
차이는 위 Track 행 수 차이의 원인이 아니다. 사후 ReID 실행은 실제 추론을
수행했으나 이 시퀀스의 `tracks`와 `tracks_reid`는 동일했고 재연결 성공
이벤트는 없었다. BoT-SORT Association 로그에는 `appearance_gate` 10건이
기록됐다. 이는 appearance가 평가된 증거이며 ID 지표 개선을 뜻하지 않는다.

원격 전체 결과: `/tmp/lwir-integrated-benchmark-20261111/results/`.
이 디렉터리에는 Detection, Track, ReID 이벤트와 profiler 요약 원본이 있다.
빌드·단위 테스트·네 실행 로그와 원격 사전/사후 상태도 이 디렉터리에 수집했다.
`frame_policy` CTest 1/1 통과. 기존 체크아웃·미추적 파일·Python 서버·짐벌
브리지·DXRT 서비스 상태가 끝난 뒤 동일함을 `resume_state_after.txt`로 확인했다.
분리 worktree는 제거됐다.

IDSW/IDF1/HOTA는 평가하지 않았다. Orange Pi와 호스트 모두 `trackeval`이
설치되지 않았으며, seq_004 `gt.txt`의 출처와 평가 조건을 별도로 확인해야 한다.
동시 NPU 사용 중이므로 이 한 번의 지연 결과만으로 성능 우열을 결론내리지 않는다.
특히 GMC 설정의 Tracking 평균 13.938 ms는 실시간 성능 검토가 필요하다.
