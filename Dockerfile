# Multi-stage build: compile in a fat image, ship a slim runtime image.
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config ca-certificates \
    libpqxx-dev libpq-dev protobuf-compiler-grpc \
    libgrpc++-dev libprotobuf-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DLEDGERCORE_ENABLE_POSTGRES=ON \
        -DLEDGERCORE_ENABLE_GRPC=ON \
        -DLEDGERCORE_BUILD_TESTS=OFF \
        -DLEDGERCORE_BUILD_BENCH=ON \
 && cmake --build build -j $(nproc) --target ledgercored bench_harness

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpqxx-7.9 libgrpc++1.51 libprotobuf32 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10001 ledger
COPY --from=build /src/build/ledgercored /usr/local/bin/ledgercored
COPY --from=build /src/build/bench_harness /usr/local/bin/bench_harness
USER 10001
EXPOSE 50051
ENTRYPOINT ["/usr/local/bin/ledgercored"]
