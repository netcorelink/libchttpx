FROM debian:bookworm-slim AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libcjson-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN make clean \
    && make lib-install DESTDIR=/pkg

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libcjson1 \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /pkg/usr/local/lib/libchttpx.so /usr/local/lib/
COPY --from=builder /pkg/usr/local/lib/pkgconfig/libchttpx.pc /usr/local/lib/pkgconfig/
COPY --from=builder /pkg/usr/local/include/libchttpx /usr/local/include/libchttpx

RUN ldconfig
