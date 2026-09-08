# TuringOS v2 build/test image (SPEC §Stack "Docker", WS9-01).
# emscripten/emsdk ships emcc + node; gcc and make are added for the native build and the tests.
FROM emscripten/emsdk:6.0.9

RUN apt-get update && \
    apt-get install -y --no-install-recommends gcc make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . /workspace

CMD ["make", "test"]
