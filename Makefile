PYTHON ?= python3
CLANG ?= clang
# Pass paths as Make variables; recipes read their exported values without shell
# interpolation. Paths with spaces (or shell metacharacters) remain plain data.
export ROMS FIRMWARE TOOLCHAIN SAMPLES GAMES CORE_SOURCE CLANG LD_LLD DISK STOCK_BACKUP
export SUNXI_TOOLS XFEL TITLE_INDEX

.PHONY: help deps menuconfig scan image verify-image sdcard check check-source preview
.PHONY: fel-image fel-sdcard fel-check backup
help:
	@$(PYTHON) tools/make_entry.py help

deps menuconfig scan image:
	@$(PYTHON) tools/make_entry.py $@

sdcard:
	@$(PYTHON) tools/write_sd.py

# FEL entry needs a dedicated spare SD; backup never programs internal flash.
fel-image fel-check backup:
	@$(PYTHON) tools/backup.py $@

fel-sdcard:
	@$(PYTHON) tools/write_sd.py --fel

verify-image:
	@$(PYTHON) tools/verify_image.py

check: check-source
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'
	$(PYTHON) tools/render_ui_preview.py --no-png
	$(PYTHON) tools/run_stock_tests.py
	$(PYTHON) tools/run_library_tests.py

check-source:
	$(PYTHON) tools/check_source.py

preview:
	$(PYTHON) tools/render_ui_preview.py
