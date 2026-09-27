ENGINE ?= lualatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := molecule-example.tex
PDF := $(BUILD_DIR)/molecule-example.pdf
BUNDLED_PDF := ../bundled/molecule-visualizer.pdf
BUNDLED_DIR := ../bundled
GENERATED_ASSETS := water-inline.xyz methane-inline.xyz carbon-dioxide-inline.xyz \
	benzene-inline.xyz
POSTER_ASSETS := methane-still.png methane-anaglyph-still.png \
	water-still.png carbon-dioxide-still.png benzene-still.png

# Override this when the CHC theme is checked out elsewhere:
#   make CHC_THEME_DIR=/path/to/lecture-slides-chemical-bonding/chctheme
CHC_THEME_DIR ?= ../../../../LATEX/lecture-slides-chemical-bonding/chctheme
UIL_LATEX_DIR := $(abspath ../../latex)
CHC_THEME_DIR_ABS := $(abspath $(CHC_THEME_DIR))
TEXMFVAR := $(abspath $(BUILD_DIR)/texmf-var)

# The trailing // lets TeX search theme subdirectories such as img/.
export TEXINPUTS := .:$(UIL_LATEX_DIR)//:$(CHC_THEME_DIR_ABS)//:
# Keep LuaTeX's generated font cache inside the example build directory.
export TEXMFVAR

.PHONY: all pdf bundle clean help check

all: bundle

pdf: $(PDF)

bundle: $(BUNDLED_PDF)

check:
	@command -v "$(ENGINE)" >/dev/null 2>&1 || { \
		echo "error: $(ENGINE) was not found" >&2; exit 1; \
	}
	@test -f "$(CHC_THEME_DIR_ABS)/chctheme.sty" || { \
		echo "error: CHC theme not found at $(CHC_THEME_DIR_ABS)" >&2; \
		echo "Set CHC_THEME_DIR=/path/to/chctheme and try again." >&2; exit 1; \
	}

$(PDF): $(SOURCE) $(POSTER_ASSETS) $(UIL_LATEX_DIR)/uilmolecule.sty | check
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

$(BUNDLED_PDF): $(PDF)
	@cmake -E copy "$(PDF)" "$(BUNDLED_PDF)"
	@cmake -E copy "$(BUILD_DIR)/water-inline.xyz" "$(BUNDLED_DIR)/water-inline.xyz"
	@cmake -E copy "$(BUILD_DIR)/carbon-dioxide-inline.xyz" "$(BUNDLED_DIR)/carbon-dioxide-inline.xyz"
	@cmake -E copy "$(BUILD_DIR)/methane-inline.xyz" "$(BUNDLED_DIR)/methane-inline.xyz"
	@cmake -E copy "$(BUILD_DIR)/benzene-inline.xyz" "$(BUNDLED_DIR)/benzene-inline.xyz"
	@echo "Built $(BUNDLED_PDF) with UIL molecule sidecars"

clean:
	rm -rf "$(BUILD_DIR)"
	rm -f molecule-example.aux molecule-example.log molecule-example.nav \
		molecule-example.out molecule-example.snm molecule-example.toc \
		$(GENERATED_ASSETS)

help:
	@echo "make              Build $(PDF) with LuaLaTeX"
	@echo "make bundle       Build bundled PDF and molecule sidecars"
	@echo "make clean        Remove generated files"
	@echo "make ENGINE=lualatex"
	@echo "make CHC_THEME_DIR=/path/to/chctheme"
