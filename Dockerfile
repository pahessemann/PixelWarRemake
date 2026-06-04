FROM debian:bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DPIXELWAR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release \
    && ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim

RUN useradd --create-home --home-dir /app --shell /usr/sbin/nologin pixelwar

WORKDIR /app
COPY --from=build /src/build/pixelwar_server /app/pixelwar_server
COPY --from=build /src/public /app/public
COPY --from=build /src/config /app/config

RUN mkdir -p /app/data \
    && chown -R pixelwar:pixelwar /app

USER pixelwar
EXPOSE 8080

CMD ["./pixelwar_server", "config/server.example.json"]
