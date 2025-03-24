CFLAGS ?= -Wall -Werror -Wextra -g -s -w
LDFLAGS ?=

CC ?= gcc

BUILDDIR ?= .
PROGS := suex sush usrx uarch
PROG ?= suex
SRCS := $(PROG).c

archs = amd64 arm64
arch ?= $(shell arch)

all: $(PROG) $(PROG)-static

.PHONY: build
build: $(PROG)

.PHONY: auth_common.o
auth_common.o: auth_common.c auth_common.h
	$(CC) $(CFLAGS) -c auth_common.c

$(PROG): $(SRCS) auth_common.o
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@ $^ $(LDFLAGS)

$(PROG)-static: $(SRCS) auth_common.o
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@ $^ -static $(LDFLAGS)

clean:
	rm -f $(PROG) $(PROG)-static *.o

fmt:
	docker run --rm -v "$$PWD":/src -w /src alpine:latest sh -c "apk add --no-cache indent && indent -linux $(SRCS) && indent -linux $(SRCS)"

fmt-all:
	$(MAKE) fmt SRCS="*.c"

release-all:
	$(MAKE) release

.PHONY: release
release: $(archs)

$(archs):
	mkdir -p release
	$(MAKE) alpine-build-docker arch=$@
	export COPYFILE_DISABLE=true; \
	for PROG in $(PROGS); do \
		tar -czf ./release/$$PROG-linux-$@.tgz LICENSE -C ./build $$PROG $$PROG-static; \
	done

alpine-build-docker:
	$(MAKE) clean
	docker run --rm -v "$$PWD":/src -w /src -e PROGS="$(PROGS)" --platform linux/$(arch) alpine:latest sh -c "./alpine-build.sh"

build-test-image:
	docker buildx build -t suex-test -f test.dockerfile --platform linux/$(arch) .

.PHONY: test-suex
test-suex:
	c=`docker run --rm -d --platform linux/$(arch) suex-test sh -c "tail -f /dev/null"`; \
	docker cp Makefile $$c:/test/ ;\
	docker cp suex-test.sh $$c:/test/ ;\
	docker cp auth_common.c $$c:/test/ ;\
	docker cp auth_common.h $$c:/test/ ;\
	docker cp suex.c $$c:/test/ ;\
	docker exec $$c make build ;\
	docker exec $$c chmod +x suex-test.sh ;\
	docker exec $$c ./suex-test.sh ;\
	docker stop $$c
