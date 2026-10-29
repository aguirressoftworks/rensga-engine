ARG BASE
FROM ${BASE}
RUN apt-get update && apt-get install -y --no-install-recommends cmake make mingw-w64 ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* \
 && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
 && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
ARG SDL2_VER=2.30.8
ARG SDL2_SHA=adbf5ae7b5144f02d4cb091a9a7f4752f8e835b4bebd9f3fcafd9d7a4194f3b9
RUN curl -fsSL -o /tmp/sdl.tgz https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VER}/SDL2-devel-${SDL2_VER}-mingw.tar.gz \
 && echo "${SDL2_SHA}  /tmp/sdl.tgz" | sha256sum -c - && mkdir -p /opt && tar -xzf /tmp/sdl.tgz -C /opt && rm /tmp/sdl.tgz
ENV SDL2_MINGW_ROOT=/opt/SDL2-${SDL2_VER}/x86_64-w64-mingw32
