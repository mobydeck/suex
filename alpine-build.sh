#!/bin/sh -ex

apk add --no-cache gcc musl-dev make

rm -rf ./build
mkdir -p ./build

for PROG in ${PROGS}; do
  BUILDDIR=./build make PROG=${PROG}
done
