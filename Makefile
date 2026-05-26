# IoT Energy Routing — NS-3.41 integration Makefile
#
# Usage:
#   export NS3_DIR=$HOME/ns-3.41
#   make install    # copy scratch files into NS-3 tree
#   make build      # configure + build simulation
#   make run        # single example run
#   make experiments # full batch via scripts/run_experiments.sh

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
NS3_DIR ?= $(HOME)/ns-3.41
SCRATCH_TARGET := $(NS3_DIR)/scratch/iot_energy_sim.cc
METRICS_TARGET := $(NS3_DIR)/scratch/iot_sim_metrics.h
SIM_NAME := iot_energy_sim

.PHONY: all install build run clean experiments check-ns3 help

all: build

help:
	@echo "IoT Energy Routing — build targets"
	@echo "  make install      Copy simulation sources into NS-3 scratch/"
	@echo "  make build        Build $(SIM_NAME) in NS-3.41"
	@echo "  make run          Run a quick AODV smoke test"
	@echo "  make experiments  Run full protocol/scalability batch"
	@echo "  make clean        Remove copied scratch files from NS-3"
	@echo ""
	@echo "Set NS3_DIR if NS-3 is not at ~/ns-3.41 (current: $(NS3_DIR))"

check-ns3:
	@test -d "$(NS3_DIR)" || (echo "ERROR: NS-3 not found at $(NS3_DIR). Set NS3_DIR." && exit 1)
	@test -f "$(NS3_DIR)/ns3" || (echo "ERROR: ns3 script missing in $(NS3_DIR)" && exit 1)

install: check-ns3
	@mkdir -p "$(NS3_DIR)/scratch"
	cp "$(PROJECT_DIR)/scratch/iot_energy_sim.cc" "$(SCRATCH_TARGET)"
	cp "$(PROJECT_DIR)/scratch/iot_sim_metrics.h" "$(METRICS_TARGET)"
	@echo "Installed scratch files to $(NS3_DIR)/scratch/"

build: install
	cd "$(NS3_DIR)" && ./ns3 configure --enable-examples --enable-tests
	cd "$(NS3_DIR)" && ./ns3 build $(SIM_NAME)
	@echo "Build complete: $(NS3_DIR)/build/scratch/$(SIM_NAME)"

run: build
	cd "$(PROJECT_DIR)" && "$(NS3_DIR)/build/scratch/ns3.41-$(SIM_NAME)-default" \
		--protocol=AODV --nNodes=20 --simTime=30 --seed=1 \
		--outputDir=results/csv

experiments:
	@chmod +x "$(PROJECT_DIR)/scripts/run_experiments.sh"
	cd "$(PROJECT_DIR)" && NS3_DIR="$(NS3_DIR)" ./scripts/run_experiments.sh

clean: check-ns3
	rm -f "$(SCRATCH_TARGET)" "$(METRICS_TARGET)"
	cd "$(NS3_DIR)" && ./ns3 clean || true
