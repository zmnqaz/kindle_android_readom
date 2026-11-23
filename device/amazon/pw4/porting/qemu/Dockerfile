FROM ubuntu:22.04
RUN sed -i 's#http://[a-z.]*ubuntu.com#http://mirrors.tuna.tsinghua.edu.cn#g' /etc/apt/sources.list && \
    apt-get update -q && DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
      qemu-system-arm gcc-arm-linux-gnueabihf libc6-dev-armhf-cross cpio gzip python3 device-tree-compiler && \
    rm -rf /var/lib/apt/lists/*
RUN useradd -u 1000 -m u
USER u
