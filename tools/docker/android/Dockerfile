ARG BASE
FROM ${BASE}
# JDK 17 (base) + Android cmdline-tools, SDK platform 34, build-tools 34.0.0, NDK 26.1.10909125, Gradle 8.5, SDL2 2.30.8 source.
RUN apt-get update && apt-get install -y --no-install-recommends unzip curl ca-certificates make \
 && rm -rf /var/lib/apt/lists/*
ENV ANDROID_HOME=/opt/android-sdk ANDROID_SDK_ROOT=/opt/android-sdk
ARG CMDLINE_SHA=2d2d50857e4eb553af5a6dc3ad507a17adf43d115264b1afc116f95c92e5e258
RUN curl -fsSL -o /tmp/c.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
 && echo "${CMDLINE_SHA}  /tmp/c.zip" | sha256sum -c - \
 && mkdir -p $ANDROID_HOME/cmdline-tools && unzip -q /tmp/c.zip -d $ANDROID_HOME/cmdline-tools \
 && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && rm /tmp/c.zip
RUN yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses >/dev/null \
 && $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "platforms;android-34" "build-tools;34.0.0" "platform-tools" "ndk;26.1.10909125" "cmake;3.22.1" >/dev/null \
 && chmod -R a+rwX $ANDROID_HOME
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/26.1.10909125
ARG GRADLE_SHA=9d926787066a081739e8200858338b4a69e837c3a821a33aca9db09dd4a41026
RUN curl -fsSL -o /tmp/g.zip https://services.gradle.org/distributions/gradle-8.5-bin.zip \
 && echo "${GRADLE_SHA}  /tmp/g.zip" | sha256sum -c - && unzip -q /tmp/g.zip -d /opt && rm /tmp/g.zip
ENV PATH=/opt/gradle-8.5/bin:$ANDROID_HOME/cmake/3.22.1/bin:$PATH
ARG SDL2_SHA=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058
RUN curl -fsSL -o /tmp/s.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-2.30.8.tar.gz \
 && echo "${SDL2_SHA}  /tmp/s.tgz" | sha256sum -c - && tar -xzf /tmp/s.tgz -C /opt && rm /tmp/s.tgz
ENV SDL2_ROOT=/opt/SDL2-2.30.8
