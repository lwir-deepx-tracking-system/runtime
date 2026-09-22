# DX-AllSuite commit 5749ab70cc75cede356c57b9cc7cf91229e47742
FROM dx-runtime:dxas-5749ab70-ubuntu22.04

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

WORKDIR /workspace/lwir-deepx-tracking-system
