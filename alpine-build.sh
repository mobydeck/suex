#!/bin/sh -ex

apk add --no-cache gcc musl-dev make

rm -rf ./build
mkdir -p ./build

BUILDDIR=./build make PROG=${PROG}
