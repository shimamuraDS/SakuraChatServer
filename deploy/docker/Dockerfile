FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    g++ cmake ninja-build pkg-config libboost-system-dev libboost-thread-dev libboost-filesystem-dev \
    libgrpc++-dev protobuf-compiler protobuf-compiler-grpc libprotobuf-dev libjsoncpp-dev \
    libhiredis-dev libmysqlcppconn-dev libsodium-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake cmake
COPY Common Common
COPY GateServer GateServer
COPY StatusServer StatusServer
COPY ChatServer ChatServer
COPY VarifyServer/message.proto VarifyServer/message.proto
COPY tests tests
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sakura \
    && cmake --build /build --parallel 2 && ctest --test-dir /build --output-on-failure \
    && cmake --install /build

FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 tini ca-certificates libboost-system1.83.0 libboost-thread1.83.0 libboost-filesystem1.83.0 \
    libgrpc++1.51t64 libprotobuf32t64 libjsoncpp25 libhiredis1.1.0 libmysqlcppconn7v5 libsodium23 \
    && rm -rf /var/lib/apt/lists/* && useradd --uid 10001 --no-create-home sakura
COPY --from=build /opt/sakura /opt/sakura
COPY deploy/docker/entrypoint.py deploy/docker/health.py /opt/sakura/
USER 10001:10001
ENV SAKURA_SECURITY_MODE=production SAKURA_LISTEN_ADDRESS=0.0.0.0
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 CMD ["python3", "/opt/sakura/health.py"]
ENTRYPOINT ["/usr/bin/tini", "--", "python3", "/opt/sakura/entrypoint.py"]
