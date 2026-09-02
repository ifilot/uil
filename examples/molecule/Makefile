ENGINE ?= lualatex
ENGINE_FLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
BUILD_DIR := build
SOURCE := molecule-example.tex
PDF := $(BUILD_DIR)/molecule-example.pdf
PACKAGE := ../bundled/molecule-visualizer.uil
PACKAGE_DIR := $(BUILD_DIR)/package
GENERATED_ASSETS := water-inline.xyz methane-inline.xyz carbon-dioxide-inline.xyz
POSTER_ASSETS := methane-still.png methane-anaglyph-still.png \
	water-still.png carbon-dioxide-still.png

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

.PHONY: all pdf package clean help check

all: package

pdf: $(PDF)

package: $(PACKAGE)

check:
	@command -v "$(ENGINE)" >/dev/null 2>&1 || { \
		echo "error: $(ENGINE) was not found" >&2; exit 1; \
	}
	@command -v zip >/dev/null 2>&1 || { \
		echo "error: zip was not found" >&2; exit 1; \
	}
	@test -f "$(CHC_THEME_DIR_ABS)/chctheme.sty" || { \
		echo "error: CHC theme not found at $(CHC_THEME_DIR_ABS)" >&2; \
		echo "Set CHC_THEME_DIR=/path/to/chctheme and try again." >&2; exit 1; \
	}

$(PDF): $(SOURCE) $(POSTER_ASSETS) $(UIL_LATEX_DIR)/uilmolecule.sty | check
	@mkdir -p "$(BUILD_DIR)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"
	$(ENGINE) $(ENGINE_FLAGS) -output-directory="$(BUILD_DIR)" "$(SOURCE)"

$(PACKAGE): $(PDF) manifest.json
	@cmake -E remove_directory "$(PACKAGE_DIR)"
	@cmake -E make_directory "$(PACKAGE_DIR)"
	@cmake -E copy "$(PDF)" "$(PACKAGE_DIR)/molecule-example.pdf"
	@cmake -E copy manifest.json "$(PACKAGE_DIR)/manifest.json"
	@cmake -E copy "$(BUILD_DIR)/water-inline.xyz" "$(PACKAGE_DIR)/water-inline.xyz"
	@cmake -E copy "$(BUILD_DIR)/carbon-dioxide-inline.xyz" "$(PACKAGE_DIR)/carbon-dioxide-inline.xyz"
	@cmake -E copy "$(BUILD_DIR)/methane-inline.xyz" "$(PACKAGE_DIR)/methane-inline.xyz"
	@cmake -E rm -f "$(abspath $(PACKAGE))"
	@cd "$(PACKAGE_DIR)" && zip -q -X "$(abspath $(PACKAGE))" \
		molecule-example.pdf manifest.json $(GENERATED_ASSETS)
	@echo "Built $(PACKAGE)"

clean:
	rm -rf "$(BUILD_DIR)"
	rm -f molecule-example.aux molecule-example.log molecule-example.nav \
		molecule-example.out molecule-example.snm molecule-example.toc \
		$(GENERATED_ASSETS)

help:
	@echo "make              Build $(PDF) with LuaLaTeX"
	@echo "make package      Build bundled $(PACKAGE)"
	@echo "make clean        Remove generated files"
	@echo "make ENGINE=lualatex"
	@echo "make CHC_THEME_DIR=/path/to/chctheme"
