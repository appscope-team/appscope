ARCH := $(shell uname -m)

GO ?= $(shell which go 2>&1)
ifeq (,$(GO))
$(error "error: \`go\` not in PATH; install or set GO to it's path")
endif

GO_MAJOR_VERSION = $(shell $(GO) version | cut -c 14- | cut -d' ' -f1 | cut -d'.' -f1)
GO_MINOR_VERSION = $(shell $(GO) version | cut -c 14- | cut -d' ' -f1 | cut -d'.' -f2)
MIN_REQ_GO_MAJOR_VERSION = 1
MIN_REQ_GO_MINOR_VERSION = 18

# cache downloaded modules and binaries locally
export GOBIN := $(shell pwd)/.gobin/$(ARCH)
export GOCACHE := $(shell pwd)/.gocache
export GOMODCACHE := $(shell pwd)/.gomod
export GOFLAGS := -modcacherw

BINDIR := ../bin/linux/$(ARCH)
LIBDIR := ../lib/linux/$(ARCH)
SRCDIR := ../src
CONFDIR := ../conf

SCOPE := $(BINDIR)/scope
SCOPEDYN := $(BINDIR)/scopedyn
LIBSCOPE := $(LIBDIR)/libscope.so
LIBLOADER := $(LIBDIR)/libloader.a
CONF_YML := $(CONFDIR)/scope.yml
LOADER_C_FILES:=$(wildcard $(SRCDIR)/loader/*.c)
LIBRARY_C_FILES:=$(wildcard $(SRCDIR)/*.c)

GO_BINDATA := $(GOBIN)/go-bindata
GOVVV := $(GOBIN)/govvv
GOLANGCI_LINT := $(GOBIN)/golangci-lint
TRIVY := $(GOBIN)/trivy

# fail if of Go is not meet version requirements
require-go-version:
	@if [ $(GO_MAJOR_VERSION) -gt $(MIN_REQ_GO_MAJOR_VERSION) ]; then \
		exit 0 ;\
	elif [ $(GO_MAJOR_VERSION) -lt $(MIN_REQ_GO_MAJOR_VERSION) ]; then \
		echo 'error: minimum supported version of Go is $(MIN_REQ_GO_MAJOR_VERSION).$(MIN_REQ_GO_MINOR_VERSION), detected is $(GO_MAJOR_VERSION).$(GO_MINOR_VERSION)';\
		exit 1; \
	elif [ $(GO_MINOR_VERSION) -lt $(MIN_REQ_GO_MINOR_VERSION) ] ; then \
		echo 'error: minimum supported version of Go is $(MIN_REQ_GO_MAJOR_VERSION).$(MIN_REQ_GO_MINOR_VERSION), detected is $(GO_MAJOR_VERSION).$(GO_MINOR_VERSION)';\
		exit 1; \
	fi

## build: build all (default)
build: all

## debug: build with gdb support
debug:
	$(MAKE) scope BUILD_OPTS="-gcflags=all=\"-N -l\""

# If any cli TestGetFlow* test fails, this may need to be rerun
# and the expected results in flows/flow_test.go will need to be
# adjusted accordingly.  Blech.
genflowtest:
	./flows/gen_flow_test.sh

## all:
all: deps $(SCOPE) require-go-version

## clean: remove the built binary
clean:
	$(RM) -r build/*
	$(RM) run/bundle.go $(SCOPE) $(SCOPEDYN)
	$(RM) coverage.out coverage.html
	$(RM) -rf -- .gocache/*/
	$(RM) -rf -- .gomod/*/
	$(RM) -rf -- .gobin/x86_64/go*
	$(RM) -rf -- .gobin/aarch64/go*
	$(RM) -rf -- bpf/vmlinux.h

## deps: install dependencies
deps: $(GO_BINDATA) $(GOVVV) $(GOLANGCI_LINT) $(TRIVY)
$(GO_BINDATA):
	$(GO) install github.com/go-bindata/go-bindata/...
$(GOVVV):
	$(GO) install github.com/ahmetb/govvv
$(GOLANGCI_LINT):
	curl -sSfL https://raw.githubusercontent.com/golangci/golangci-lint/master/install.sh | \
		sh -s -- -b $(GOBIN) v1.42.1
$(TRIVY):
	curl -sfL https://raw.githubusercontent.com/aquasecurity/trivy/main/contrib/install.sh | \
		sh -s -- -b $(GOBIN) v0.38.3

rebuild: clean build 

# in case someone is building here before the C code is built
$(LIBSCOPE): $(LIBRARY_C_FILES)
	@$(MAKE) -C .. lib/linux/$(ARCH)/libscope.so
$(LIBLOADER): $(LOADER_C_FILES)
	@$(MAKE) -C .. lib/linux/$(ARCH)/libloader.a
$(SCOPEDYN): $(LOADER_C_FILES)
	@$(MAKE) -C .. bin/linux/$(ARCH)/scopedyn

# the -w go build flags remove the dwarf symbol table from the resulting executable
# the -tags force os/user and netgo to use go and NOT cgo because a static exec can't resolve libc calls from cgo
# To enable building SIGDEL, replace next line with: $(SCOPE): $(LIBSCOPE) $(LIBLOADER) $(SCOPEDYN) $(SIGDEL)
$(SCOPE): $(LIBSCOPE) $(LIBLOADER) $(SCOPEDYN)
$(SCOPE): export GOOS=linux
$(SCOPE): export CGO_ENABLED=1
$(SCOPE): $(GOVVV) $(filter-out %_test.go,$(shell find . -type f -name '*.go'))
	@echo "$${CI:+::group::}Building scope"
	$(RM) run/bundle.go
	$(RM) -r build/*
	cp $(LIBSCOPE) $(CONF_YML) $(SCOPEDYN) build/
	$(GO_BINDATA) -nocompress -o run/bundle.go -pkg run build
	CGO_CFLAGS="-DLIBSCOPE_SO_SIZE=\"$(shell du -b "$(LIBSCOPE)" | cut -f1)\" -DSCOPEDYN_SIZE=\"$(shell du -b "$(SCOPEDYN)" | cut -f1)\"" \
	$(GO) build \
		-tags "osusergo netgo" \
		-ldflags="$(shell $(GOVVV) -flags) -w -linkmode external -extldflags \"-lrt -ldl -lpthread -static\" " \
		-buildmode=pie \
		$(BUILD_OPTS) \
		-o $(SCOPE)
	$(RM) -r $(SCOPEDYN)

## test: run unit tests
test:
	CGO_CFLAGS="-DLIBSCOPE_SO_SIZE=\"0\" -DSCOPEDYN_SIZE=\"0\"" \
	$(GO) test -v -p 1 -failfast -timeout 600s -coverprofile coverage.out ./...

## fmt: format all Go files
fmt:
	$(GO) fmt ./...

## lint: lint all Go files
lint: $(GOLANGCI_LINT)
	$(GOLANGCI_LINT) --new-from-rev=HEAD~1 run ./...

## vet: vet all Go files
vet: 
	$(GO) vet ./...

## cover: produce a test coverage report
cover: 
	$(GO) tool cover -html=coverage.out -o coverage.html

## check: format, vet, lint and test all Go files
check: fmt vet lint test

## trivy: 
trivy:
	$(TRIVY) fs . --ignore-unfixed --skip-dirs .gomod

help: Makefile
	@echo
	@echo "Choose a command to run:"
	@echo
	@sed -n 's/^##//p' $< | column -t -s ':' |  sed -e 's/^/ /'
	@echo

# fail of upx not in $PATH
require-upx:
	@[ -n "$(shell which upx)" ] || \
	  { echo >&2 "error: upx required"; exit 1; }

.PHONY: build debug all clean deps rebuild scope test fmt lint vet cover check help
