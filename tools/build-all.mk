# tools/build-all.mk — drive tools/zbpm-build + tools/zbpm-repo for every
# package listed in tools/packages.list.
#
# Usage:
#     make -f tools/build-all.mk REPO=./repo all          # build everything
#     make -f tools/build-all.mk REPO=./repo bash         # one package
#     make -f tools/build-all.mk REPO=./repo clean        # nuke staging
#     make -j8 -f tools/build-all.mk REPO=./repo all      # parallel
#
# Variables:
#   REPO         destination repo directory (required for any non-clean target)
#   STAGING      where zbpm-build writes per-package staging dirs (default: ./staging)
#   PACKAGES     path to manifest (default: tools/packages.list)
#   ZBPM_BUILD   path to companion build tool (default: tools/zbpm-build)
#   ZBPM_REPO    path to repo manager       (default: tools/zbpm-repo)
#
# Parallel safety: the per-package "build" step runs in its own staging
# subdirectory and is independent. The "add" step mutates REPO/index.txt and
# is therefore serialised through `flock` on REPO/.lock so that `make -jN`
# is safe even though the underlying tool is not internally locked.

SHELL          := /bin/bash
.SHELLFLAGS    := -eu -o pipefail -c
.DELETE_ON_ERROR:
MAKEFLAGS      += --no-builtin-rules

TOOLS_DIR      := $(dir $(lastword $(MAKEFILE_LIST)))
TOOLS_DIR      := $(patsubst %/,%,$(TOOLS_DIR))

PACKAGES       ?= $(TOOLS_DIR)/packages.list
ZBPM_BUILD     ?= $(TOOLS_DIR)/zbpm-build
ZBPM_REPO      ?= $(TOOLS_DIR)/zbpm-repo
STAGING        ?= ./staging
REPO           ?=

# Parse packages.list:
#   strip comments / blank lines, take col1 as dnf name and col2 (or col1) as
#   the zbpm package name. Produce two parallel lists.
_PKG_LINES     := $(shell sed -e 's/\#.*$$//' -e '/^[[:space:]]*$$/d' $(PACKAGES) 2>/dev/null \
                       | awk '{ if (NF>=2) printf "%s:%s\n",$$1,$$2; else printf "%s:%s\n",$$1,$$1 }')

DNF_NAMES      := $(foreach e,$(_PKG_LINES),$(word 1,$(subst :, ,$(e))))
ZBPM_NAMES     := $(foreach e,$(_PKG_LINES),$(word 2,$(subst :, ,$(e))))

# Per-package stamp marking a successful 'zbpm-repo add'.
STAMPS         := $(foreach n,$(ZBPM_NAMES),$(STAGING)/.stamp-$(n))

.PHONY: all clean clean-stamps list help $(ZBPM_NAMES)

all: $(STAMPS)
	@echo "build-all: $(words $(STAMPS)) package(s) in $(REPO)"

help:
	@echo "Targets:"
	@echo "  all            build + ingest every package in $(PACKAGES)"
	@echo "  <pkgname>      build + ingest a single package"
	@echo "  list           print parsed package list"
	@echo "  clean          remove $(STAGING)/"
	@echo "  clean-stamps   force re-ingest on next run"
	@echo ""
	@echo "Required: REPO=<dir>     (e.g. make -f tools/build-all.mk REPO=./repo all)"
	@echo "Optional: STAGING=<dir>  (default: $(STAGING))"
	@echo "          -j<N>          (parallel; add is flock-serialised)"

list:
	@printf 'dnf-name           zbpm-name\n'
	@printf -- '------------------ ------------------\n'
	@paste <(printf '%s\n' $(DNF_NAMES)) <(printf '%s\n' $(ZBPM_NAMES))

# Convenience: `make -f tools/build-all.mk bash` is shorthand for the stamp.
define _PKG_ALIAS_template
$(1): $$(STAGING)/.stamp-$(1)
.PHONY: $(1)
endef
$(foreach n,$(ZBPM_NAMES),$(eval $(call _PKG_ALIAS_template,$(n))))

# Map zbpm-name -> dnf-name for the build step. We pre-compute pairs so the
# rule recipe can do an O(1) lookup with `awk`.
_PAIRS_FILE    := $(STAGING)/.pairs
$(_PAIRS_FILE): $(PACKAGES)
	@mkdir -p $(STAGING)
	@: > $@.tmp
	@$(foreach e,$(_PKG_LINES),printf '%s %s\n' '$(word 2,$(subst :, ,$(e)))' '$(word 1,$(subst :, ,$(e)))' >> $@.tmp;)
	@mv -f $@.tmp $@

# Pattern rule: build the staging dir, then ingest into the repo under a flock.
$(STAGING)/.stamp-%: $(_PAIRS_FILE) | _check_repo
	@dnfname=$$(awk -v p='$*' '$$1==p {print $$2; exit}' $(_PAIRS_FILE)); \
	if [ -z "$$dnfname" ]; then echo "build-all: no entry for $*" >&2; exit 1; fi; \
	outdir="$(STAGING)"; \
	stagedir="$$outdir/$*"; \
	mkdir -p "$$outdir"; \
	rm -rf "$$stagedir"; \
	echo "==> building $* (from dnf:$$dnfname)"; \
	"$(ZBPM_BUILD)" --out-dir "$$outdir" --zbpm-name "$*" "$$dnfname"; \
	[ -f "$$stagedir/$*.tar.gz" ] || { echo "build-all: $(ZBPM_BUILD) did not produce $$stagedir/$*.tar.gz" >&2; exit 1; }; \
	[ -f "$$stagedir/$*.version" ] || { echo "build-all: $(ZBPM_BUILD) did not produce $$stagedir/$*.version" >&2; exit 1; }; \
	echo "==> ingesting $* into $(REPO)"; \
	mkdir -p "$(REPO)"; \
	( flock 9; "$(ZBPM_REPO)" add "$(REPO)" "$$stagedir"; ) 9> "$(REPO)/.lock"; \
	mkdir -p "$(dir $@)"; touch "$@"

# Guard target: REPO must be set for any build. Errors immediately if not.
.PHONY: _check_repo
_check_repo:
	@if [ -z "$(REPO)" ]; then \
	    echo "build-all: REPO is not set. Pass REPO=<dir> on the make command line." >&2; \
	    exit 2; \
	fi; \
	if [ ! -x "$(ZBPM_BUILD)" ]; then \
	    echo "build-all: $(ZBPM_BUILD) is missing or not executable" >&2; \
	    exit 2; \
	fi; \
	if [ ! -x "$(ZBPM_REPO)" ]; then \
	    echo "build-all: $(ZBPM_REPO) is missing or not executable" >&2; \
	    exit 2; \
	fi; \
	if [ ! -d "$(REPO)" ] || [ ! -f "$(REPO)/index.txt" ]; then \
	    echo "==> initialising repo at $(REPO)"; \
	    "$(ZBPM_REPO)" init "$(REPO)"; \
	fi

clean-stamps:
	@rm -f $(STAGING)/.stamp-*
	@echo "build-all: removed stamps in $(STAGING)/"

clean:
	@rm -rf $(STAGING)
	@echo "build-all: removed $(STAGING)/"
