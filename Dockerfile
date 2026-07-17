# syntax=docker/dockerfile:1

# Dev base image: prebuilt libchttpx + toolchain for compiling C/C++ apps.
#
# Usage in other projects:
#   FROM noneandundefined/libchttpx:latest
#   WORKDIR /app
#   COPY . .
#   RUN gcc main.c $(pkg-config --cflags --libs libchttpx) -lcjson -o app

FROM debian:bookworm-slim AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libcjson-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY Makefile libchttpx.pc ./
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

RUN make test \
    && make lib-install DESTDIR=/out PREFIX=/usr/local

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libcjson-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /out/usr/local/ /usr/local/

RUN ldconfig \
    && pkg-config --exists libchttpx \
    && test -f /usr/local/lib/libchttpx.so \
    && test -d /usr/local/include/libchttpx

LABEL org.opencontainers.image.source="https://github.com/netcorelink/libchttpx" \
      org.opencontainers.image.description="libchttpx C HTTP/WebSocket library (dev base image)" \
      org.opencontainers.image.licenses="MIT"

WORKDIR /app
