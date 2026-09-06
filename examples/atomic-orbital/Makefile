ENGINE ?= pdflatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := atomic-orbitals-example.tex
ORBITALS := 2s.uilorb 2pz.uilorb 3dz2.uilorb 4fz3.uilorb
PREVIEWS := previews/2s.png previews/2pz.png previews/3dz2.png previews/4fz3.png
PDF := $(BUILD_DIR)/atomic-orbitals-example.pdf
BUNDLED_PDF := ../bundled/atomic-orbitals.pdf
UIL_LATEX_DIR := $(abspath ../../latex)

export TEXINPUTS := .:$(UIL_LATEX_DIR)//:

.PHONY: all pdf bundle clean check

all: bundle

pdf: $(PDF)

bundle: $(BUNDLED_PDF)

check:
	@command -v "$(ENGINE)" >/dev/null 2>&1 || { \
		echo "error: $(ENGINE) was not found" >&2; exit 1; \
	}

$(PDF): $(SOURCE) $(ORBITALS) $(PREVIEWS) $(UIL_LATEX_DIR)/uilorbital.sty | check
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

$(BUNDLED_PDF): $(PDF)
	@cmake -E copy "$(PDF)" "$(BUNDLED_PDF)"
	@echo "Built $(BUNDLED_PDF) with four embedded atomic orbitals"

clean:
	rm -rf "$(BUILD_DIR)"
