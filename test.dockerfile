FROM almalinux:10

RUN dnf install -y gcc make sudo procps-ng

WORKDIR /test
