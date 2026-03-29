BUILD_DIR ?= build
DATASETS_FILE ?= datasets.mk

include $(DATASETS_FILE)

MKLIST := tools/mklist.sh
MK_SAMPLE := $(BUILD_DIR)/bin/mk_sample
SAMPLES_BUILD_DIR ?= $(BUILD_DIR)/samples
SAMPLES_STAMP_EXT ?= .stamp
RUN_DB_PATH ?= $(AMARANTIN_RUN_DB)
DEFAULT_ORIGIN ?= data
DEFAULT_VARIATION ?= nominal
DEFAULT_BEAM ?= numi
DEFAULT_POLARITY ?= fhc

ALL_SAMPLE_LISTS :=
ALL_SAMPLE_ROOTS :=
ALL_SAMPLE_STAMPS :=

define SAMPLE_RULE
ALL_SAMPLE_LISTS += $(out.$(1))/$(2).list
ALL_SAMPLE_ROOTS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root
ALL_SAMPLE_STAMPS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root$(SAMPLES_STAMP_EXT)

$(out.$(1))/$(2).list: $(MKLIST)
	$(MKLIST) --dir "$(base.$(1))/$(3)" --pat "$(pat)" --out "$$@"

$(SAMPLES_BUILD_DIR)/$(1)/$(2).root: $(out.$(1))/$(2).list $(MK_SAMPLE) $(DATASETS_FILE) samples-dag.mk
	@set -eu; \
	out="$$@"; \
	list_file="$$<"; \
	stamp_file="$$@$(SAMPLES_STAMP_EXT)"; \
	tmp_stamp="$$$$stamp_file.tmp"; \
	if command -v sha256sum >/dev/null 2>&1; then \
		hash_file() { sha256sum "$$$$1" | awk '{print $$$$1}'; }; \
	elif command -v shasum >/dev/null 2>&1; then \
		hash_file() { shasum -a 256 "$$$$1" | awk '{print $$$$1}'; }; \
	else \
		echo "samples-dag: need sha256sum or shasum" >&2; \
		exit 1; \
	fi; \
	run_db_args=""; \
	if [ -n '$(RUN_DB_PATH)' ]; then \
		run_db_args="--run-db $(RUN_DB_PATH)"; \
	fi; \
	mkdir -p "$$$${out%/*}"; \
	{ \
		printf 'list_sha256=%s\n' "$$$$(hash_file "$$$$list_file")"; \
		printf 'mk_sample_sha256=%s\n' "$$$$(hash_file "$(MK_SAMPLE)")"; \
		printf 'origin=%s\n' '$(DEFAULT_ORIGIN)'; \
		printf 'variation=%s\n' '$(DEFAULT_VARIATION)'; \
		printf 'beam=%s\n' '$(DEFAULT_BEAM)'; \
		printf 'polarity=%s\n' '$(DEFAULT_POLARITY)'; \
		printf 'run_db=%s\n' '$(RUN_DB_PATH)'; \
		printf 'dataset=%s\n' '$(1)'; \
		printf 'sample=%s\n' '$(2)'; \
		printf 'sample_subdir=%s\n' '$(3)'; \
	} > "$$$$tmp_stamp"; \
	if [ -f "$$$$out" ] && [ -f "$$$$stamp_file" ] && cmp -s "$$$$tmp_stamp" "$$$$stamp_file"; then \
		rm -f "$$$$tmp_stamp"; \
		echo "samples-dag: $$$$out is up to date"; \
	else \
		"$(MK_SAMPLE)" $$$$run_db_args \
			"$$$$out" "$$$$list_file" \
			"$(DEFAULT_ORIGIN)" "$(DEFAULT_VARIATION)" "$(DEFAULT_BEAM)" "$(DEFAULT_POLARITY)"; \
		mv "$$$$tmp_stamp" "$$$$stamp_file"; \
	fi
endef

$(foreach ds,$(datasets),$(foreach item,$(samples.$(ds)),$(eval $(call SAMPLE_RULE,$(ds),$(word 1,$(subst :, ,$(item))),$(word 2,$(subst :, ,$(item)))))))

.PHONY: samples samples-lists samples-clean print-samples print-sample-stamps

samples: $(MK_SAMPLE) $(ALL_SAMPLE_ROOTS)

samples-lists: $(ALL_SAMPLE_LISTS)

$(MK_SAMPLE):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel --target mk_sample

samples-clean:
	rm -rf $(SAMPLES_BUILD_DIR)
	rm -f $(ALL_SAMPLE_LISTS)

print-samples:
	@printf '%s\n' $(ALL_SAMPLE_ROOTS)

print-sample-stamps:
	@printf '%s\n' $(ALL_SAMPLE_STAMPS)
