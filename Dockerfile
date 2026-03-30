FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc \
        make \
        python3 \
        python3-pygame && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . /workspace

CMD ["make", "run"]
