ROOT_CONFIG ?= root-config
ifeq ($(shell command -v $(ROOT_CONFIG) 2>/dev/null),)
$(error ROOT not found. Please set up ROOT so that 'root-config' is on your PATH.)
endif

CXX ?= $(shell $(ROOT_CONFIG) --cxx)
BUILD_DIR ?= ../build

CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fPIC $(shell $(ROOT_CONFIG) --cflags)
LDLIBS ?= $(shell $(ROOT_CONFIG) --libs) -lsqlite3

LIB := $(BUILD_DIR)/lib/libIO.so
SRC := $(wildcard source/*.cc)
OBJ := $(patsubst source/%.cc,$(BUILD_DIR)/obj/io/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

.PHONY: all clean
all: $(LIB)

$(LIB): $(OBJ)
	mkdir -p $(dir $@)
	$(CXX) -shared $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/obj/io/%.o: source/%.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(LIB) $(OBJ) $(DEP)

-include $(DEP)
