# Dockerfile
FROM debian:bookworm-slim

# Dependencias de build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Copiar proyecto y compilar
WORKDIR /app
COPY . /app
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j && \
    strip build/linkchat && \
    mv build/linkchat /usr/local/bin/linkchat

# Directorio por defecto para descargas
RUN mkdir -p /inbox
VOLUME ["/inbox"]

# REPL por defecto
ENTRYPOINT ["/usr/local/bin/linkchat"]
