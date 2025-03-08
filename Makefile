CFLAGS ?= -Wall -Werror -g -s -w
LDFLAGS ?=

CC ?= gcc

BUILDDIR ?= .
PROG := suex
SRCS := $(PROG).c

archs = amd64 arm64

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

.PHONY: release
release: $(archs)

$(archs):
	mkdir -p release
	$(MAKE) alpine-build-docker arch=$@
	COPYFILE_DISABLE=true \
	tar -czf ./release/$(PROG)-linux-$@.tgz LICENSE -C ./build $(PROG) $(PROG)-static

alpine-build-docker:
	docker run --rm -v "$$PWD":/src -w /src --platform linux/$(arch) alpine:latest sh -c "./alpine-build.sh $(PROG) $(SRCS)"
