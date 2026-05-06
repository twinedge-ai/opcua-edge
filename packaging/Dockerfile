FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libmodbus-dev \
    libsqlite3-dev \
    pkg-config \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmodbus5 \
    libsqlite3-0 \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/* \
 && groupadd -g 10001 edge \
 && useradd -u 10001 -g 10001 -r -s /usr/sbin/nologin -d /app edge
WORKDIR /app
COPY --from=build /src/build/opcua-edge /app/opcua-edge
COPY templates /app/templates
COPY packaging/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh \
 && mkdir -p /data/templates /data/db \
 && chown -R 10001:10001 /data
VOLUME ["/data"]
EXPOSE 4840
USER 10001:10001
HEALTHCHECK --interval=10s --timeout=2s --start-period=5s --retries=3 \
  CMD nc -z 127.0.0.1 ${EDGE_OPCUA_PORT:-4840} || exit 1
ENTRYPOINT ["/app/entrypoint.sh"]
CMD []
