# 외부 의존성

독립 저장소에서는 고정된 dx_app 소스를 `third_party/dx_app`에 둡니다.
공식 저장소와 현재 검증 기준 버전은 다음과 같습니다.

- URL: `https://github.com/DEEPX-AI/dx_app.git`
- 태그: `v3.2.2`
- 커밋: `01b77271c3cf9f59c3440a953fe56d99adfbda12`

```bash
git submodule add https://github.com/DEEPX-AI/dx_app.git third_party/dx_app
git -C third_party/dx_app checkout 01b77271c3cf9f59c3440a953fe56d99adfbda12
git submodule update --init --recursive
```

외부 설치본을 사용하는 경우에는 CMake에
`-DDX_APP_ROOT=/설치/또는/체크아웃/경로`를 전달할 수 있습니다.
