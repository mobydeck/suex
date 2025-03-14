CFLAGS ?= -Wall -Werror -g -s -w
LDFLAGS ?=

CC ?= gcc

BUILDDIR ?= .
PROG ?= suex
SRCS := $(PROG).c

archs = amd64 arm64
arch ?= $(shell arch)

all: $(PROG) $(PROG)-static

.PHONY: build
build: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@ $^ $(LDFLAGS)

$(PROG)-static: $(SRCS)
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@ $^ -static $(LDFLAGS)

clean:
	rm -f $(PROG) $(PROG)-static

fmt:
	docker run --rm -v "$$PWD":/src -w /src alpine:latest sh -c "apk add --no-cache indent && indent -linux $(SRCS)"

release-all:
	$(MAKE) release
	$(MAKE) release PROG=usrx
	$(MAKE) release PROG=uarch

.PHONY: release
release: $(archs)

$(archs):
	mkdir -p release
	$(MAKE) alpine-build-docker arch=$@ PROG=$(PROG)
	COPYFILE_DISABLE=true \
	tar -czf ./release/$(PROG)-linux-$@.tgz LICENSE -C ./build $(PROG) $(PROG)-static

alpine-build-docker:
	docker run --rm -v "$$PWD":/src -w /src -e PROG=$(PROG) --platform linux/$(arch) alpine:latest sh -c "./alpine-build.sh"

check:
	docker run --rm -v "$$PWD":/src -w /src -e PROG=usrx --platform linux/$(arch) alpine:latest sh -c "set -x && ./alpine-build.sh && (./build/usrx-static info -i root)"