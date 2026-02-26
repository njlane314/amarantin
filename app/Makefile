ROOT_CONFIG ?= root-config
ifeq ($(shell command -v $(ROOT_CONFIG) 2>/dev/null),)
$(error ROOT not found. Please set up ROOT so that 'root-config' is on your PATH.)
endif

CXX ?= $(shell $(ROOT_CONFIG) --cxx)
BUILD_DIR ?= ../build

CPPFLAGS += -I../io/include
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra $(shell $(ROOT_CONFIG) --cflags)
LDLIBS ?= $(shell $(ROOT_CONFIG) --libs) -lsqlite3

BIN := $(BUILD_DIR)/bin/mk_sample
OBJ := $(BUILD_DIR)/obj/app/mk_sample.o
DEP := $(OBJ:.o=.d)

.PHONY: all io clean
all: $(BIN)

io:
	$(MAKE) -C ../io ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=$(BUILD_DIR) all

$(BIN): io $(OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJ) -L$(BUILD_DIR)/lib -lIO $(LDLIBS) -o $@

$(BUILD_DIR)/obj/app/%.o: %.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

-include $(DEP)
