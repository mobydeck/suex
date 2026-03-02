#!/bin/sh -ex

rm -rf ./build
mkdir -p ./build

for PROG in ${PROGS}; do
  BUILDDIR=./build make PROG=${PROG}
done
