# SPDX-License-Identifier: GPL-3.0-only
# hello-az: build the mod inside the SDK container.
#
#   <sdk>/tools/azmod-pack build .   builds in the container (recommended)
#   make                             only inside the container (or another aarch64
#                                    Ubuntu 18.04 toolchain); azmod-pack runs this
#   make DEBUG=1 OUT=out-debug       a copy with debug info (-g) for addr2line: the same code
#                                    at the same addresses as the plain build, plus line tables
#   make test [SAN=plain|asan|tsan]  on your computer, with clang: build the mod for the
#                                    host and run it under the SDK's test harness, plain,
#                                    with AddressSanitizer + UBSan, or with ThreadSanitizer
#                                    (default: all three). AZ_SDK is the SDK checkout.
#
# The flags below are the rules from docs/GUIDELINES.md in Makefile form:
#   -fvisibility=hidden          export only az_mod_v1_entry (rule: one symbol)
#   -fPIC -shared                a shared library the core can dlopen
#   -static-libgcc               no libgcc_s dependency
#   -Wl,--no-undefined           a missing function fails the build, not the deck
#   -Werror=implicit-function-declaration   same reason, at compile time
#   -fno-omit-frame-pointer     if your mod ever crashes the deck, the core can trace it
#                                back to your mod (and only your mod is switched off)
#   -fno-optimize-sibling-calls  a core call as the last statement of a callback still
#                                counts as your mod's call (the core checks who called)
#   -fexceptions -fasynchronous-unwind-tables   the deck's own code throws C++
#                                exceptions through call chains your code can sit in;
#                                without unwind tables that becomes a crash
#   -mno-outline-atomics        (only when the compiler knows it) keeps atomics inline
#

MOD_ID          := hello-az
SRCS            := hello-az.c
AZ_SDK_INCLUDE  ?= ../include
AZ_SDK          ?= ..
MAX_REDRAWS     ?= 4          # the most repaints a second your tick asks for
SAN             ?= plain asan tsan
TEST_CC         ?= clang
OUT             ?= out
DEBUG           ?= 0          # 1: add -g (debug info only; the code is the same)

# make's built-in default is cc; use gcc unless CC was given on the command line
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Werror=implicit-function-declaration \
           -fPIC -fvisibility=hidden -fno-omit-frame-pointer \
           -fno-optimize-sibling-calls \
           -fexceptions -fasynchronous-unwind-tables \
           -I$(AZ_SDK_INCLUDE)
LDFLAGS := -shared -static-libgcc -Wl,--no-undefined -Wl,--hash-style=gnu
LDLIBS  :=        # add -lpthread if you start a thread, -lm for math functions

# GCC 10+ on aarch64 defaults to outline atomics (a libgcc helper). The SDK's GCC 7
# does not know the flag; probe for it so the same Makefile works on both.
NO_OUTLINE_ATOMICS := $(shell printf 'int main(void){return 0;}\n' | \
    $(CC) -mno-outline-atomics -x c -o /dev/null - 2>/dev/null && echo -mno-outline-atomics)
CFLAGS += $(NO_OUTLINE_ATOMICS)

ifeq ($(strip $(DEBUG)),1)
CFLAGS   += -g
endif

comma := ,
space := $(subst ,, )

.PHONY: all clean test
all: $(OUT)/$(MOD_ID).so

$(OUT)/$(MOD_ID).so: $(SRCS) $(AZ_SDK_INCLUDE)/azmod-mod.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

$(OUT):
	mkdir -p $(OUT)

test:
	@test -f $(AZ_SDK)/tools/harness/sanitize.sh || \
	    { echo "make test: set AZ_SDK to your az-mods-sdk checkout" >&2; exit 2; }
	CC=$(TEST_CC) sh $(AZ_SDK)/tools/harness/sanitize.sh --passes "$(SAN)" \
	    $(MOD_ID):$(strip $(MAX_REDRAWS)):$(subst $(space),$(comma),$(strip $(SRCS)))

clean:
	rm -rf $(OUT)
