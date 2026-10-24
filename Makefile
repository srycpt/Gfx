CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?=
LDLIBS_COMMON = -lm
SPACE_DIR = effects/space/hall
SPACE_SRC = $(SPACE_DIR)/gfx_space.c
SPACE_HDR = $(SPACE_DIR)/gfx_space.h
INCLUDES = -I$(SPACE_DIR)
BUILD_DIR = build

all: $(BUILD_DIR)/gfx_listen $(BUILD_DIR)/gfx_test_space

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/gfx_listen: tools/gfx_listen/gfx_listen.c $(SPACE_SRC) $(SPACE_HDR) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ tools/gfx_listen/gfx_listen.c $(SPACE_SRC) -lsndfile $(LDLIBS_COMMON)

$(BUILD_DIR)/gfx_test_space: tests/space/gfx_test_space.c $(SPACE_SRC) $(SPACE_HDR) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ tests/space/gfx_test_space.c $(SPACE_SRC) $(LDLIBS_COMMON)

test: $(BUILD_DIR)/gfx_test_space
	cd $(BUILD_DIR) && ./gfx_test_space

clean:
	rm -rf $(BUILD_DIR)
	rm -f space_impulse.csv $(BUILD_DIR)/space_impulse.csv

.PHONY: all test clean
