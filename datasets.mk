user := nlane
release := v08_00_00_82
pat := nu_selection.root,nu_selection_data.root

datasets := run1

root := /pnfs/uboone/scratch/users/$(user)/ntuples/$(release)

name.run1 := numi_fhc_run1

base.run1 := $(root)/$(name.run1)

out.run1 := samplelists/$(name.run1)

samples.run1 := \
	beam-s0:beam/s0/out 