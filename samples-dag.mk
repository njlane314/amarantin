BUILD_DIR ?= build
DATASETS_FILE ?= datasets.mk

include $(DATASETS_FILE)

MKLIST := tools/mklist.sh
MK_SAMPLE := $(BUILD_DIR)/bin/mk_sample
MK_DATASET := $(BUILD_DIR)/bin/mk_dataset
SAMPLES_BUILD_DIR ?= $(BUILD_DIR)/samples
DATASETS_BUILD_DIR ?= $(BUILD_DIR)/datasets
SAMPLES_STAMP_EXT ?= .stamp
RUN_DB_PATH ?= $(AMARANTIN_RUN_DB)
DEFAULT_ORIGIN ?= data
DEFAULT_VARIATION ?= nominal
DEFAULT_BEAM ?= numi
DEFAULT_POLARITY ?= fhc
DEFAULT_SOURCE_KIND ?= dir

sample_item_key = $(word 1,$(subst :, ,$(1)))
sample_item_legacy_ref = $(word 2,$(subst :, ,$(1)))

sample_origin_for = $(if $(value sample_origin.$(1).$(2)),$(value sample_origin.$(1).$(2)),$(DEFAULT_ORIGIN))
sample_variation_for = $(if $(value sample_variation.$(1).$(2)),$(value sample_variation.$(1).$(2)),$(DEFAULT_VARIATION))
sample_beam_for = $(if $(value sample_beam.$(1).$(2)),$(value sample_beam.$(1).$(2)),$(DEFAULT_BEAM))
sample_polarity_for = $(if $(value sample_polarity.$(1).$(2)),$(value sample_polarity.$(1).$(2)),$(DEFAULT_POLARITY))
sample_source_kind_for = $(if $(value sample_source_kind.$(1).$(2)),$(value sample_source_kind.$(1).$(2)),$(if $(3),dir,$(DEFAULT_SOURCE_KIND)))
sample_source_ref_for = $(if $(value sample_source_ref.$(1).$(2)),$(value sample_source_ref.$(1).$(2)),$(3))
sample_source_base_for = $(if $(value sample_source_base.$(1).$(2)),$(value sample_source_base.$(1).$(2)),$(base.$(1)))
sample_list_path_for = $(out.$(1))/$(2).list
sample_manifest_for = $(value sample_manifest.$(1).$(2))
sample_artifacts_for = $(value sample_artifacts.$(1).$(2))
sample_artifact_lists_for = $(foreach artifact,$(call sample_artifacts_for,$(1),$(2)),$(call sample_list_path_for,$(1),$(artifact)))
dataset_defs_for = $(value dataset_defs.$(1))
dataset_manifest_for = $(value dataset_manifest.$(1))
dataset_sample_roots_for = $(value DATASET_SAMPLE_ROOTS.$(1))
dataset_run_for = $(value dataset_run.$(1))
dataset_beam_for = $(value dataset_beam.$(1))
dataset_polarity_for = $(value dataset_polarity.$(1))
dataset_campaign_for = $(value dataset_campaign.$(1))

ALL_SAMPLE_LISTS :=
ALL_SAMPLE_ROOTS :=
ALL_SAMPLE_STAMPS :=
ALL_DATASET_ROOTS :=

define SAMPLE_LIST_RULE
ALL_SAMPLE_LISTS += $(call sample_list_path_for,$(1),$(2))

$(call sample_list_path_for,$(1),$(2)): $(MKLIST)
	@set -eu; \
	source_kind='$(call sample_source_kind_for,$(1),$(2),$(3))'; \
	source_ref='$(call sample_source_ref_for,$(1),$(2),$(3))'; \
	source_base='$(call sample_source_base_for,$(1),$(2))'; \
	case "$$$$source_kind" in \
		dir) \
			"$(MKLIST)" --dir "$$$$source_base/$$$$source_ref" --pat "$(pat)" --out "$$@" ;; \
		list) \
			"$(MKLIST)" --list "$$$$source_ref" --out "$$@" ;; \
		samdef) \
			"$(MKLIST)" --samdef "$$$$source_ref" --out "$$@" ;; \
		*) \
			echo "samples-dag: unknown source kind '$$$$source_kind' for $(1):$(2)" >&2; \
			exit 1 ;; \
	esac
endef

define LEGACY_SAMPLE_RULE
ALL_SAMPLE_ROOTS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root
ALL_SAMPLE_STAMPS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root$(SAMPLES_STAMP_EXT)
DATASET_SAMPLE_ROOTS.$(1) += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root

$(SAMPLES_BUILD_DIR)/$(1)/$(2).root: $(call sample_list_path_for,$(1),$(2)) $(MK_SAMPLE) $(DATASETS_FILE) samples-dag.mk
	@set -eu; \
	out="$$@"; \
	list_file="$(call sample_list_path_for,$(1),$(2))"; \
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
			printf 'source_kind=%s\n' '$(call sample_source_kind_for,$(1),$(2),$(3))'; \
			printf 'source_ref=%s\n' '$(call sample_source_ref_for,$(1),$(2),$(3))'; \
			printf 'source_base=%s\n' '$(call sample_source_base_for,$(1),$(2))'; \
			printf 'pattern=%s\n' '$(pat)'; \
			printf 'origin=%s\n' '$(call sample_origin_for,$(1),$(2))'; \
			printf 'variation=%s\n' '$(call sample_variation_for,$(1),$(2))'; \
			printf 'beam=%s\n' '$(call sample_beam_for,$(1),$(2))'; \
			printf 'polarity=%s\n' '$(call sample_polarity_for,$(1),$(2))'; \
			printf 'run_db=%s\n' '$(RUN_DB_PATH)'; \
			printf 'dataset=%s\n' '$(1)'; \
			printf 'sample=%s\n' '$(2)'; \
		} > "$$$$tmp_stamp"; \
	if [ -f "$$$$out" ] && [ -f "$$$$stamp_file" ] && cmp -s "$$$$tmp_stamp" "$$$$stamp_file"; then \
		rm -f "$$$$tmp_stamp"; \
		echo "samples-dag: $$$$out is up to date"; \
	else \
		"$(MK_SAMPLE)" $$$$run_db_args \
			"$$$$out" "$$$$list_file" \
			"$(call sample_origin_for,$(1),$(2))" \
			"$(call sample_variation_for,$(1),$(2))" \
			"$(call sample_beam_for,$(1),$(2))" \
			"$(call sample_polarity_for,$(1),$(2))"; \
		mv "$$$$tmp_stamp" "$$$$stamp_file"; \
	fi
endef

define LOGICAL_SAMPLE_RULE
ALL_SAMPLE_ROOTS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root
ALL_SAMPLE_STAMPS += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root$(SAMPLES_STAMP_EXT)
DATASET_SAMPLE_ROOTS.$(1) += $(SAMPLES_BUILD_DIR)/$(1)/$(2).root

$(SAMPLES_BUILD_DIR)/$(1)/$(2).root: $(call sample_artifact_lists_for,$(1),$(2)) $(call sample_manifest_for,$(1),$(2)) $(MK_SAMPLE) $(DATASETS_FILE) samples-dag.mk
	@set -eu; \
	out="$$@"; \
	manifest_file="$(call sample_manifest_for,$(1),$(2))"; \
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
			printf 'manifest_sha256=%s\n' "$$$$(hash_file "$$$$manifest_file")"; \
			for list_file in $(call sample_artifact_lists_for,$(1),$(2)); do \
				printf 'list_sha256[%s]=%s\n' "$$$$list_file" "$$$$(hash_file "$$$$list_file")"; \
			done; \
			printf 'mk_sample_sha256=%s\n' "$$$$(hash_file "$(MK_SAMPLE)")"; \
			printf 'pattern=%s\n' '$(pat)'; \
			printf 'origin=%s\n' '$(call sample_origin_for,$(1),$(2))'; \
			printf 'variation=%s\n' '$(call sample_variation_for,$(1),$(2))'; \
			printf 'beam=%s\n' '$(call sample_beam_for,$(1),$(2))'; \
			printf 'polarity=%s\n' '$(call sample_polarity_for,$(1),$(2))'; \
			printf 'run_db=%s\n' '$(RUN_DB_PATH)'; \
			printf 'dataset=%s\n' '$(1)'; \
			printf 'sample=%s\n' '$(2)'; \
			printf 'manifest=%s\n' '$(call sample_manifest_for,$(1),$(2))'; \
		} > "$$$$tmp_stamp"; \
	if [ -f "$$$$out" ] && [ -f "$$$$stamp_file" ] && cmp -s "$$$$tmp_stamp" "$$$$stamp_file"; then \
		rm -f "$$$$tmp_stamp"; \
		echo "samples-dag: $$$$out is up to date"; \
	else \
		"$(MK_SAMPLE)" $$$$run_db_args \
			--sample "$(2)" \
			--manifest "$$$$manifest_file" \
			"$$$$out" \
			"$(call sample_origin_for,$(1),$(2))" \
			"$(call sample_variation_for,$(1),$(2))" \
			"$(call sample_beam_for,$(1),$(2))" \
			"$(call sample_polarity_for,$(1),$(2))"; \
		mv "$$$$tmp_stamp" "$$$$stamp_file"; \
	fi
endef

define DATASET_RULE
ifneq ($(strip $(call dataset_manifest_for,$(1))),)
ALL_DATASET_ROOTS += $(DATASETS_BUILD_DIR)/$(1).root

$(DATASETS_BUILD_DIR)/$(1).root: $(call dataset_sample_roots_for,$(1)) $(MK_DATASET) $(call dataset_defs_for,$(1)) $(call dataset_manifest_for,$(1)) $(DATASETS_FILE) samples-dag.mk
	@set -eu; \
	out="$$@"; \
	mkdir -p "$$$${out%/*}"; \
	if [ -n '$(call dataset_run_for,$(1))' ] && [ -n '$(call dataset_beam_for,$(1))' ] && [ -n '$(call dataset_polarity_for,$(1))' ]; then \
		campaign_args=""; \
		if [ -n '$(call dataset_campaign_for,$(1))' ]; then \
			campaign_args="--campaign $(call dataset_campaign_for,$(1))"; \
		fi; \
		"$(MK_DATASET)" \
			--defs "$(call dataset_defs_for,$(1))" \
			$$$$campaign_args \
			--run "$(call dataset_run_for,$(1))" \
			--beam "$(call dataset_beam_for,$(1))" \
			--polarity "$(call dataset_polarity_for,$(1))" \
			--manifest "$(call dataset_manifest_for,$(1))" \
			"$$$$out"; \
	else \
		"$(MK_DATASET)" \
			--defs "$(call dataset_defs_for,$(1))" \
			--manifest "$(call dataset_manifest_for,$(1))" \
			"$$$$out" "$(name.$(1))"; \
	fi
endif
endef

$(foreach ds,$(datasets),$(foreach item,$(samples.$(ds)),$(eval $(call SAMPLE_LIST_RULE,$(ds),$(call sample_item_key,$(item)),$(call sample_item_legacy_ref,$(item))))))
$(foreach ds,$(datasets),$(if $(strip $(value logical_samples.$(ds))),$(foreach logical,$(logical_samples.$(ds)),$(eval $(call LOGICAL_SAMPLE_RULE,$(ds),$(logical)))),$(foreach item,$(samples.$(ds)),$(eval $(call LEGACY_SAMPLE_RULE,$(ds),$(call sample_item_key,$(item)),$(call sample_item_legacy_ref,$(item)))))))
$(foreach ds,$(datasets),$(eval $(call DATASET_RULE,$(ds))))

.PHONY: samples samples-lists samples-clean datasets datasets-clean print-samples print-sample-stamps print-datasets

samples: $(MK_SAMPLE) $(ALL_SAMPLE_ROOTS)

samples-lists: $(ALL_SAMPLE_LISTS)

datasets: $(MK_DATASET) $(ALL_DATASET_ROOTS)

$(MK_SAMPLE):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel --target mk_sample

$(MK_DATASET):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel --target mk_dataset

samples-clean:
	rm -rf $(SAMPLES_BUILD_DIR)
	rm -f $(ALL_SAMPLE_LISTS)

datasets-clean:
	rm -rf $(DATASETS_BUILD_DIR)

print-samples:
	@printf '%s\n' $(ALL_SAMPLE_ROOTS)

print-sample-stamps:
	@printf '%s\n' $(ALL_SAMPLE_STAMPS)

print-datasets:
	@printf '%s\n' $(ALL_DATASET_ROOTS)
