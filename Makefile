ROOT_CONFIG ?= root-config
BUILD_DIR ?= build

.PHONY: all io app clean
all: io app

io:
	$(MAKE) -C io ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=../$(BUILD_DIR) all

app:
	$(MAKE) -C app ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=../$(BUILD_DIR) all

clean:
	$(MAKE) -C app ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=../$(BUILD_DIR) clean
	$(MAKE) -C io ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=../$(BUILD_DIR) clean
