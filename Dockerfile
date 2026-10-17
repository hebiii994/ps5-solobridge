# Ambiente di build per payload PS5 (ps5-payload-dev/sdk)
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash clang-18 lld-18 llvm-18 wget unzip make ca-certificates \
        socat cmake meson pkg-config python3 python3-pyelftools netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

ENV LLVM_CONFIG=/usr/bin/llvm-config-18
RUN wget -q https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip -O /tmp/sdk.zip \
    && unzip -q -d /opt /tmp/sdk.zip \
    && rm /tmp/sdk.zip

ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
WORKDIR /work
