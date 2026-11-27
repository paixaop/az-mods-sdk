# SPDX-License-Identifier: GPL-3.0-only
# analysis-client: build the mod inside the SDK container.
#
#   <sdk>/tools/azmod-pack build .   builds in the container (recommended)
#   make                             only inside the container (or another aarch64
#                                    Ubuntu 18.04 toolchain); azmod-pack runs this
#   make DEBUG=1 OUT=out-debug       a copy with debug info (-g) for addr2line: the same code
#                                    at the same addresses as the plain build, plus line tables
#   make test [SAN=plain|asan|tsan]  on your computer, with clang: build and run the SDK's
#                                    C fixture for the analysis service lifecycle, plain,
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

MOD_ID          := analysis-client
SRCS            := example.c
AZ_SDK          ?= ../..
AZ_SDK_INCLUDE  ?= $(AZ_SDK)/include
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
TEST_FIXTURE := $(AZ_SDK)/tests/fixtures/analysis-client.c
TEST_TARGETS := test-plain test-asan test-tsan
SAN_FLAGS_asan := -fsanitize=address,undefined -fno-sanitize-recover=all
SAN_FLAGS_tsan := -fsanitize=thread

.PHONY: all clean test $(TEST_TARGETS)
all: $(OUT)/$(MOD_ID).so

$(OUT)/$(MOD_ID).so: $(SRCS) $(AZ_SDK_INCLUDE)/azmod-mod.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

$(OUT):
	mkdir -p $(OUT)

test: $(addprefix test-,$(subst $(comma),$(space),$(SAN)))

$(TEST_TARGETS): test-%: $(OUT)/test-%
	"$<"

$(OUT)/test-%: $(TEST_FIXTURE) $(SRCS) $(AZ_SDK_INCLUDE)/azmod-mod.h Makefile | $(OUT)
	$(TEST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer \
	    $(SAN_FLAGS_$*) -DAZ_ANALYSIS_CLIENT_LOCAL_SOURCE -I. -I"$(AZ_SDK_INCLUDE)" "$<" -o "$@"

clean:
	rm -rf $(OUT)
