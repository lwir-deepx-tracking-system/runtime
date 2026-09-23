# DX-AllSuite commit 5749ab70cc75cede356c57b9cc7cf91229e47742
# 사내 registry를 사용하면 빌드 인자로 digest가 고정된 이미지를 전달한다.
ARG DX_RUNTIME_IMAGE=dx-runtime:dxas-5749ab70-ubuntu22.04
FROM ${DX_RUNTIME_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    gdb \
    pkg-config \
    tree \
    libyaml-cpp-dev \
    libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/lwir-runtime
