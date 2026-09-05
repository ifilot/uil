ENGINE ?= pdflatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := interactive-figure-example.tex
FIGURES := moving-wave.uilfig harmonic-wavepacket.uilfig harmonic-basis-states.uilfig \
	particle-in-box-step-expansion.uilfig
PDF := $(BUILD_DIR)/interactive-figure-example.pdf
UIL_LATEX_DIR := $(abspath ../../latex)

export TEXINPUTS := .:$(UIL_LATEX_DIR)//:

.PHONY: all clean

all: $(PDF)

$(PDF): $(SOURCE) $(FIGURES) $(UIL_LATEX_DIR)/uilfigure.sty
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

clean:
	rm -rf "$(BUILD_DIR)"
