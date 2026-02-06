FROM espressif/idf:v5.5.1

USER root
RUN <<EOF
    apt-get update
    apt-get install -y \
        clang-format \
        locales \
        sudo \
        udev
    locale-gen en_US.UTF-8
    update-locale LANG=en_US.UTF-8
EOF

RUN <<EOF
    # Add user account to sudoers
    echo ubuntu ' ALL = NOPASSWD: ALL' > /etc/sudoers.d/ubuntu
    chmod 0440 /etc/sudoers.d/ubuntu
EOF

USER ubuntu

RUN <<EOF
    # Add statesmith
    cd /home/ubuntu
    wget https://github.com/StateSmith/StateSmith/releases/download/cli-v0.19.0/statesmith-linux-x64.tar.gz
    tar -xvzf statesmith-linux-x64.tar.gz
    rm statesmith-linux-x64.tar.gz

    chmod +x ss.cli

    sudo mv ss.cli /usr/local/bin
EOF

RUN echo "source /opt/esp/entrypoint.sh\nset +e" > ~/.bashrc

ENTRYPOINT ["/bin/bash", "-c"]
SHELL ["/bin/bash", "-c"]
CMD ["bin/bash"]
