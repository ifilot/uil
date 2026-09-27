ENGINE ?= pdflatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := atomic-orbitals-example.tex
ATLAS_DIR := ../../latex/uilorbital-atlas
ATLAS_FILES := $(shell find $(ATLAS_DIR) -type f)
PDF := $(BUILD_DIR)/atomic-orbitals-example.pdf
BUNDLED_PDF := ../bundled/atomic-orbitals.pdf
UIL_LATEX_DIR := $(abspath ../../latex)

export TEXINPUTS := .:$(UIL_LATEX_DIR)//:

.PHONY: all pdf bundle atlas clean check

all: bundle atlas

pdf: $(PDF)

bundle: $(BUNDLED_PDF)

check:
	@command -v "$(ENGINE)" >/dev/null 2>&1 || { \
		echo "error: $(ENGINE) was not found" >&2; exit 1; \
	}

$(PDF): $(SOURCE) $(ATLAS_FILES) $(UIL_LATEX_DIR)/uilorbital.sty | check
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

$(BUNDLED_PDF): $(PDF)
	@cmake -E copy "$(PDF)" "$(BUNDLED_PDF)"
	@echo "Built $(BUNDLED_PDF) with six embedded atomic orbital plots"

clean:
	rm -rf "$(BUILD_DIR)"

# The complete, browsable Garnet-Slate catalog (55 independently embedded plots).
atlas: ../bundled/orbital-atlas.pdf

$(BUILD_DIR)/orbital-atlas.pdf: orbital-atlas.tex $(ATLAS_FILES) $(UIL_LATEX_DIR)/uilorbital.sty | check
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" orbital-atlas.tex

../bundled/orbital-atlas.pdf: $(BUILD_DIR)/orbital-atlas.pdf
	@cmake -E copy "$<" "$@"
