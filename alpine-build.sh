#!/bin/sh -ex

apk add --no-cache gcc musl-dev make

for PROG in ${PROGS}; do
  make PROG=${PROG}
done
