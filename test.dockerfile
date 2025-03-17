FROM almalinux:9

RUN dnf install -y gcc make sudo procps-ng

WORKDIR /test
