# Multi-stage build. HAVE_GPIO is OFF in containers (no GPIO), so the LogSafetyIo
# backend is used and the code is fully portable. aarch64 native on the Pi.
FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build build

FROM debian:trixie-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/src/supervisor /src/build/src/worker /usr/local/bin/
