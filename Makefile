PYTHON ?= python3
CLANG ?= clang
# Pass paths as Make variables; recipes read their exported values without shell
# interpolation. Paths with spaces (or shell metacharacters) remain plain data.
export ROMS FIRMWARE TOOLCHAIN SAMPLES GAMES CORE_SOURCE CLANG LD_LLD DISK

.PHONY: help deps menuconfig scan image verify-image sdcard check check-source preview
help:
	@$(PYTHON) tools/make_entry.py help

deps menuconfig scan image:
	@$(PYTHON) tools/make_entry.py $@

sdcard:
	@$(PYTHON) tools/write_sd.py

verify-image:
	@$(PYTHON) tools/verify_image.py

check: check-source
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'
	$(PYTHON) tools/render_ui_preview.py --no-png

check-source:
	$(PYTHON) tools/check_source.py

preview:
	$(PYTHON) tools/render_ui_preview.py
