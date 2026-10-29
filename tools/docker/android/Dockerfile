ARG BASE
FROM ${BASE}
# JDK 17 (base) + Android cmdline-tools 11076708, Gradle 8.5, SDL2 2.30.8 source. The SDK components (platform 34,
# build-tools 34.0.0, NDK 26.1.10909125, cmake 3.22.1) are ~3 GB, so tools/android/provision_sdk.sh installs them
# once into build/android-sdk, which android.sh mounts at /opt/android-sdk (keeps the image small on a tight disk).
RUN apt-get update && apt-get install -y --no-install-recommends unzip curl ca-certificates make \
 && rm -rf /var/lib/apt/lists/*
ARG CMDLINE_SHA=2d2d50857e4eb553af5a6dc3ad507a17adf43d115264b1afc116f95c92e5e258
RUN curl -fsSL -o /tmp/c.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
 && echo "${CMDLINE_SHA}  /tmp/c.zip" | sha256sum -c - \
 && mkdir -p /opt/cmdline-tools && unzip -q /tmp/c.zip -d /opt/cmdline-tools && rm /tmp/c.zip
ARG GRADLE_SHA=9d926787066a081739e8200858338b4a69e837c3a821a33aca9db09dd4a41026
RUN curl -fsSL -o /tmp/g.zip https://services.gradle.org/distributions/gradle-8.5-bin.zip \
 && echo "${GRADLE_SHA}  /tmp/g.zip" | sha256sum -c - && unzip -q /tmp/g.zip -d /opt && rm /tmp/g.zip
ARG SDL2_SHA=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058
RUN curl -fsSL -o /tmp/s.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-2.30.8.tar.gz \
 && echo "${SDL2_SHA}  /tmp/s.tgz" | sha256sum -c - && tar -xzf /tmp/s.tgz -C /opt && rm /tmp/s.tgz
# The project needs CMake >= 3.24; the SDK only ships 3.22.1 (kept for its ninja).
ARG CMAKE_SHA=931e3c0d546ee03ca72bb147ccd9b49e3b6252f765f66bf21b9d165519940458
RUN curl -fsSL -o /tmp/k.tgz https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-x86_64.tar.gz \
 && echo "${CMAKE_SHA}  /tmp/k.tgz" | sha256sum -c - && tar -xzf /tmp/k.tgz -C /opt && mv /opt/cmake-3.28.6-linux-x86_64 /opt/cmake-3.28.6 && rm /tmp/k.tgz
ENV ANDROID_HOME=/opt/android-sdk ANDROID_SDK_ROOT=/opt/android-sdk ANDROID_NDK_HOME=/opt/android-sdk/ndk/26.1.10909125 \
    SDL2_ROOT=/opt/SDL2-2.30.8 PATH=/opt/cmake-3.28.6/bin:/opt/gradle-8.5/bin:/opt/android-sdk/cmake/3.22.1/bin:/opt/cmdline-tools/cmdline-tools/bin:$PATH
