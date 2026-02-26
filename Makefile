SHELL := /bin/bash
.ONESHELL:
.DELETE_ON_ERROR:

include datasets.mk

mklist := tools/mklist.sh

CXX := g++
ROOTCFLAGS := $(shell root-config --cflags)
ROOTLIBS := $(shell root-config --libs --glibs)
SQLITELIBS := $(shell pkg-config --libs sqlite3 2>/dev/null || echo -lsqlite3)

CXXFLAGS := -O2 -g -std=c++17 -Wall -Wextra $(ROOTCFLAGS) -Iframework/io/include
LDFLAGS := $(ROOTLIBS) $(SQLITELIBS)

libdir := build/lib
objdir := build/obj

target := $(libdir)/libIO.a
bindir := build/bin
sample_tool := $(bindir)/mk_sample

srcs := \
	framework/io/source/SampleIO.cc \
	framework/io/source/ArtProvenanceIO.cc \
	framework/io/source/RunDatabaseService.cc

objs := $(srcs:%.cc=$(objdir)/%.o)
deps := $(objs:.o=.d)

FORCE:

listfiles = $(foreach sp,$(samples.$(1)),$(out.$(1))/$(word 1,$(subst :, ,$(sp))).list)
listsall := $(foreach r,$(datasets),$(call listfiles,$(r)))

list_to_sample = $(patsubst samplelists/%.list,build/sample/%.sample.root,$(1))
samplesall := $(foreach l,$(listsall),$(call list_to_sample,$(l)))

.PHONY: samples
samples: $(samplesall)

build/sample/%.sample.root: samplelists/%.list $(target) $(sample_tool) framework/io/macro/mk_sample.C
	@mkdir -p "$(dir $@)"
	# $(sample_tool) "$@" "$<" "" "data" "nominal" "numi" "fhc"
	@echo "sample generation command temporarily commented out"



.DEFAULT_GOAL := all

.PHONY: all lists clean cleanlists $(datasets)
all: $(target) $(sample_tool)

lists: $(listsall)

$(foreach r,$(datasets),$(eval $(r): $(call listfiles,$(r))))

$(target): $(objs)
	mkdir -p $(dir $@)
	ar rcs $@ $^

$(objdir)/%.o: %.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(sample_tool): framework/io/app/mk_sample.cc $(target)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -L$(libdir) -lIO $(LDFLAGS) -o $@

-include $(deps)

define makelist
$(out.$(1))/$(word 1,$(subst :, ,$(2))).list: FORCE $(mklist)
	@mkdir -p "$(out.$(1))"
	@./$(mklist) --dir "$(base.$(1))/$(word 2,$(subst :, ,$(2)))" --pat "$(pat)" --out "$$@"
endef

$(foreach r,$(datasets),$(foreach sp,$(samples.$(r)),$(eval $(call makelist,$(r),$(sp)))))

clean:
	rm -rf build

cleanlists:
	@for r in $(datasets); do rm -rf "$(out.$$r)"; done
