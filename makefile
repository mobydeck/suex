CFLAGS ?= -Wall -Werror -Wextra -g -s -w
LDFLAGS ?=

CC ?= gcc

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=

IMAGE = suex-builder
BUILDDIR ?= ./build
PROGS := suex sush usrx uarch
AUTH_PROGS := suex sush
PROG ?= suex
SRCS := $(PROG).c
AUTH_DEPS := $(if $(filter $(PROG),$(AUTH_PROGS)),auth_common.o,)

archs = amd64 arm64
arch ?= $(shell arch)

.PHONY: build
build: builddir $(BUILDDIR)/$(PROG)

.PHONY: all
all: builddir
	for prog in $(PROGS); do $(MAKE) clean && $(MAKE) PROG=$$prog; done

.PHONY: auth_common.o
auth_common.o: auth_common.c auth_common.h
	$(CC) $(CFLAGS) -c auth_common.c

STATIC ?= -static

$(BUILDDIR)/$(PROG): $(SRCS) $(AUTH_DEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(STATIC) $(LDFLAGS)
	strip -s $@

.PHONY: install
install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 4755 $(BUILDDIR)/suex $(DESTDIR)$(BINDIR)/suex
	install -m 4755 $(BUILDDIR)/sush $(DESTDIR)$(BINDIR)/sush
	install -m 755 $(BUILDDIR)/usrx $(DESTDIR)$(BINDIR)/usrx
	install -m 755 $(BUILDDIR)/uarch $(DESTDIR)$(BINDIR)/uarch

.PHONY: builddir
builddir:
	if [ ! -d $(BUILDDIR) ]; then mkdir -p $(BUILDDIR); fi

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/suex $(DESTDIR)$(BINDIR)/sush
	rm -f $(DESTDIR)$(BINDIR)/usrx $(DESTDIR)$(BINDIR)/uarch

clean:
	rm -f *.o *.c~

distclean: clean
	rm -f $(addprefix $(BUILDDIR)/,$(PROGS)) $(addprefix $(BUILDDIR)/,$(addsuffix -static,$(PROGS)))

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
	$(MAKE) build-docker arch=$@
	export COPYFILE_DISABLE=true; \
	for PROG in $(PROGS); do \
		tar -czf ./release/$$PROG-linux-$@.tgz LICENSE -C ./build $$PROG; \
	done

.PHONY: build-image
build-image:
	docker build -t $(IMAGE):$(arch) --platform linux/$(arch) -f build.dockerfile .

build-docker: build-image
	$(MAKE) distclean
	docker run --rm -v "$$PWD":/src -w /src -e PROGS="$(PROGS)" \
	    --platform linux/$(arch) $(IMAGE):$(arch) make all

test-install-docker: build-image
	$(MAKE) distclean
	docker run --rm -v "$$PWD":/src -w /src -e BUILDDIR="/tmp/build" \
	    --platform linux/$(arch) $(IMAGE):$(arch) make install

build-test-image:
	docker buildx build -t suex-test -f test.dockerfile --platform linux/$(arch) --load .

.PHONY: test-suex
test-suex: build-test-image
	@set -e; \
	c=$$(docker run --rm -d --platform linux/$(arch) suex-test sh -c "tail -f /dev/null"); \
	trap "docker stop $$c >/dev/null" EXIT; \
	tar -cf - makefile suex-test.sh auth_common.c auth_common.h suex.c | docker exec -i $$c tar -xf - -C /test; \
	docker exec $$c make build BUILDDIR=. STATIC=; \
	docker exec $$c ./suex-test.sh
