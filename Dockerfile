FROM xanderhendriks/stm32cube-extension:1.0

USER root
RUN <<EOF
    apt-get update
    apt-get install -y \
        ca-certificates \
        clang-format \
        cmake \
        curl \
        file \
        libstdc++6 \
        libgcc-s1 \
        libncurses6 \
        ninja-build \
        locales \
        sudo \
        udev \
        wget \
        xz-utils
    locale-gen en_US.UTF-8
    update-locale LANG=en_US.UTF-8
EOF

ENV ZEPHYR_SDK_VERSION=0.16.5
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ARG ZEPHYR_SDK_URL

RUN <<EOF
    set -euxo pipefail
    mkdir -p /opt
    SDK_URL="${ZEPHYR_SDK_URL:-https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz}"
    curl -fL --retry 5 --retry-delay 2 -o /tmp/zephyr-sdk.tar.xz "${SDK_URL}"
    tar -C /opt -xf /tmp/zephyr-sdk.tar.xz
    rm /tmp/zephyr-sdk.tar.xz
    ${ZEPHYR_SDK_INSTALL_DIR}/setup.sh -t all -h -c
    chown -R "user:user" "${ZEPHYR_SDK_INSTALL_DIR}"
EOF

RUN <<EOF
    pip3 install --break-system-packages \
        pyelftools \
        west
EOF

RUN <<EOF
    # Add user account to sudoers
    echo 'user ALL = NOPASSWD: ALL' > /etc/sudoers.d/user
    chmod 0440 /etc/sudoers.d/user
EOF

USER user

ENTRYPOINT ["/bin/bash", "-c"]
SHELL ["/bin/bash", "-c"]
CMD ["bin/bash"]
