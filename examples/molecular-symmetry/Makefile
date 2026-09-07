ENGINE ?= pdflatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := molecular-symmetry-example.tex
PAYLOADS := water.uilsym ammonia.uilsym boron-trifluoride.uilsym ethylene.uilsym methane.uilsym
PDF := $(BUILD_DIR)/molecular-symmetry-example.pdf
UIL_LATEX_DIR := $(abspath ../../latex)

export TEXINPUTS := .:$(UIL_LATEX_DIR)//:

.PHONY: all bundle clean

all: $(PDF)

$(PDF): $(SOURCE) $(PAYLOADS) $(UIL_LATEX_DIR)/uilsymmetry.sty
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

bundle: $(PDF)
	cp "$(PDF)" ../bundled/molecular-symmetry.pdf

clean:
	rm -rf "$(BUILD_DIR)"
