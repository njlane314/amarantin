ROOT_CONFIG ?= root-config
BUILD_DIR ?= build
DATASETS_FILE ?= datasets.mk

include $(DATASETS_FILE)

MKLIST := tools/mklist.sh
MK_SAMPLE := $(BUILD_DIR)/bin/mk_sample
SAMPLES_BUILD_DIR ?= $(BUILD_DIR)/samples

ALL_SAMPLE_LISTS :=
ALL_SAMPLE_ROOTS :=

define SAMPLE_RULE
ALL_SAMPLE_LISTS += $(out.$(1))/$(2).list
ALL_SAMPLE_ROOTS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root

$(out.$(1))/$(2).list: $(MKLIST)
	$(MKLIST) --dir "$(base.$(1))/$(3)" --pat "$(pat)" --out "$$@"

$(SAMPLES_BUILD_DIR)/$(1)/$(2).root: $(out.$(1))/$(2).list $(MK_SAMPLE)
	mkdir -p $$(dir $$@)
	$(MK_SAMPLE) "$$@" "$$<"
endef

$(foreach ds,$(datasets),$(foreach item,$(samples.$(ds)),$(eval $(call SAMPLE_RULE,$(ds),$(word 1,$(subst :, ,$(item))),$(word 2,$(subst :, ,$(item)))))))

.PHONY: samples samples-lists samples-clean print-samples

samples: $(MK_SAMPLE) $(ALL_SAMPLE_ROOTS)

samples-lists: $(ALL_SAMPLE_LISTS)

$(MK_SAMPLE):
	$(MAKE) -C app ROOT_CONFIG=$(ROOT_CONFIG) BUILD_DIR=../$(BUILD_DIR) all

samples-clean:
	rm -rf $(SAMPLES_BUILD_DIR)
	rm -f $(ALL_SAMPLE_LISTS)

print-samples:
	@printf '%s\n' $(ALL_SAMPLE_ROOTS)
