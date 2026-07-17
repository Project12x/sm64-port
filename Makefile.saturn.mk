SATURN_REPO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
LIBYAUL_DIR := $(SATURN_REPO_ROOT)/third_party/libyaul
HELLO_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hello
HWTEST_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hwtest
PYTHON ?= python3

LIBYAUL_VERSION := 0.3.1
LIBYAUL_COMMIT := 6012f79f237773378c8014e70d8998ad95a38d98

.PHONY: all bootstrap check check-libyaul check-sdk hello verify-hello hwtest verify-hwtest verify-tools clean

all: hello

bootstrap:
	@sh "$(SATURN_REPO_ROOT)/tools/saturn/bootstrap-toolchain.sh"

check: check-libyaul

check-libyaul:
	@if [ ! -e "$(LIBYAUL_DIR)/.git" ]; then \
		printf '%s\n' \
		  'libyaul is not initialized.' \
		  'Run: git submodule update --init third_party/libyaul' >&2; \
		exit 1; \
	fi
	@actual_commit=$$(git -c safe.directory="$(LIBYAUL_DIR)" \
	  -C "$(LIBYAUL_DIR)" rev-parse HEAD); \
	if [ "$$actual_commit" != "$(LIBYAUL_COMMIT)" ]; then \
		printf 'libyaul commit mismatch: expected %s, found %s\n' \
		  '$(LIBYAUL_COMMIT)' "$$actual_commit" >&2; \
		exit 1; \
	fi
	@actual_version=$$(git -c safe.directory="$(LIBYAUL_DIR)" \
	  -C "$(LIBYAUL_DIR)" show HEAD:VERSION); \
	if [ "$$actual_version" != "$(LIBYAUL_VERSION)" ]; then \
		printf 'libyaul version mismatch: expected %s, found %s\n' \
		  '$(LIBYAUL_VERSION)' "$$actual_version" >&2; \
		exit 1; \
	fi
	@printf 'libyaul %s pinned at %s\n' \
	  '$(LIBYAUL_VERSION)' '$(LIBYAUL_COMMIT)'

check-sdk:
	@if [ -z "$(YAUL_INSTALL_ROOT)" ]; then \
		printf '%s\n' \
		  'YAUL_INSTALL_ROOT is not set.' \
		  'Source your .yaul.env, then retry the build.' \
		  'See docs/saturn/BUILDING.md.' >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(YAUL_INSTALL_ROOT)/share/build.pre.mk" ] || \
	   [ ! -f "$(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk" ]; then \
		printf 'No installed libyaul build fragments found under %s/share\n' \
		  '$(YAUL_INSTALL_ROOT)' >&2; \
		printf '%s\n' 'Install the pinned submodule as described in docs/saturn/BUILDING.md.' >&2; \
		exit 1; \
	fi

hello: check-libyaul check-sdk
	$(MAKE) -C "$(HELLO_DIR)"

verify-hello: check-libyaul check-sdk
	$(MAKE) -C "$(HELLO_DIR)" verify

hwtest: check-libyaul check-sdk
	$(MAKE) -C "$(HWTEST_DIR)"

verify-hwtest: check-libyaul check-sdk
	$(MAKE) -C "$(HWTEST_DIR)" verify

verify-tools:
	$(PYTHON) "$(SATURN_REPO_ROOT)/tools/saturn/test_tools.py"

clean: check-sdk
	$(MAKE) -C "$(HELLO_DIR)" clean
