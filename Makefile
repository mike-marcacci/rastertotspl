# rastertotspl - native CUPS filter for JIOSE J500 / TSPL label printers

PREFIX     ?= /Library/Printers/rastertotspl
PPD_DIR    ?= /Library/Printers/PPDs/Contents/Resources
PPD_NAME   ?= JIOSE J500 rastertotspl.ppd.gz
ARCHS      ?= -arch arm64 -arch x86_64
CFLAGS     ?= -O2 -Wall -Wextra -Werror
CFLAGS     += -Wno-deprecated-declarations -mmacosx-version-min=11.0
LDLIBS     += -lcups

BUILD := build

all: $(BUILD)/rastertotspl $(BUILD)/rastertotspl.ppd

$(BUILD):
	mkdir -p $@

$(BUILD)/rastertotspl: src/rastertotspl.c | $(BUILD)
	$(CC) $(CFLAGS) $(ARCHS) $< $(LDLIBS) -o $@

$(BUILD)/rastertotspl.ppd: ppd/genppd.py | $(BUILD)
	python3 ppd/genppd.py $(PREFIX)/rastertotspl > $@

# Committed copy of the PPD, for people who install by hand.
ppd/rastertotspl.ppd: ppd/genppd.py
	python3 ppd/genppd.py $(PREFIX)/rastertotspl > $@

$(BUILD)/gen: tests/gen.c | $(BUILD)
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

test: $(BUILD)/rastertotspl $(BUILD)/gen $(BUILD)/rastertotspl.ppd
	tests/run.sh $(BUILD)/rastertotspl $(BUILD)/gen $(BUILD)/rastertotspl.ppd
	cupstestppd -q -W all $(BUILD)/rastertotspl.ppd

# Requires Rosetta 2 and the vendor driver; see tests/record-fixtures.sh.
record-fixtures: $(BUILD)/gen $(BUILD)/rastertotspl.ppd
	tests/record-fixtures.sh $(BUILD)/gen $(BUILD)/rastertotspl.ppd

install: all
	install -d -o root -g wheel -m 755 $(PREFIX) $(PPD_DIR)
	install -o root -g wheel -m 755 $(BUILD)/rastertotspl $(PREFIX)/rastertotspl
	gzip -9nc $(BUILD)/rastertotspl.ppd > "$(PPD_DIR)/$(PPD_NAME)"
	chown root:wheel "$(PPD_DIR)/$(PPD_NAME)"; chmod 644 "$(PPD_DIR)/$(PPD_NAME)"
	@echo "Installed. Add the printer in System Settings and pick \"JIOSE J500 TSPL (rastertotspl, native)\"."

uninstall:
	rm -f $(PREFIX)/rastertotspl "$(PPD_DIR)/$(PPD_NAME)"
	-rmdir $(PREFIX)

clean:
	rm -rf $(BUILD)

.PHONY: all test record-fixtures install uninstall clean
