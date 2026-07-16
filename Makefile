CC ?= cc
AR ?= ar
SDK_DIR ?= ../msys-sdk
# Resolve the shared UI runtime next to the selected SDK root.  Native Windows
# sync builds use an absolute SDK_DIR while their temporary source stage lives
# below .sync/, so a source-relative ../msys-ui-lvgl would point at the wrong
# directory.
UI_DIR ?= $(abspath $(SDK_DIR)/../msys-ui-lvgl)
BUILD_DIR ?= build
BIN_DIR ?= bin

CPPFLAGS += -Iinclude -Igenerated -I$(SDK_DIR)/include
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDLIBS += $(SDK_DIR)/build/libmsys-mipc.a -lX11 -ldl

SOURCES := src/main.c src/model.c src/catalog.c src/preferences.c src/image.c \
	src/notification.c src/clock.c src/system_metrics.c src/launcher_layout.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
TARGET := $(BIN_DIR)/msys-shell-native
UI_LIBRARY := $(UI_DIR)/build/libmsys-ui-lvgl.a
LVGL_TARGET := $(BIN_DIR)/msys-shell-lvgl
LVGL_PACKAGE_TARGET := files/bin/msys-shell-lvgl
LVGL_CPPFLAGS := -I$(UI_DIR)/include -I$(UI_DIR)/vendor/lvgl -I$(UI_DIR) \
	-DLV_CONF_INCLUDE_SIMPLE
LVGL_SOURCES := src/lvgl_main.c src/model.c src/catalog.c src/image.c \
	src/system_metrics.c
LVGL_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/lvgl/%.o,$(LVGL_SOURCES))
LVGL_UI_FILES := files/share/ui/shell/launcher.xml \
	files/share/ui/shell/chrome.xml files/share/ui/shell/navigation.xml \
	files/share/ui/shell/overview.xml
TEST_TARGET := $(BUILD_DIR)/test-model
CATALOG_TEST_TARGET := $(BUILD_DIR)/test-catalog
PREFERENCES_TEST_TARGET := $(BUILD_DIR)/test-preferences
IMAGE_TEST_TARGET := $(BUILD_DIR)/test-image
I18N_TEST_TARGET := $(BUILD_DIR)/test-i18n
NOTIFICATION_TEST_TARGET := $(BUILD_DIR)/test-notification
CLOCK_TEST_TARGET := $(BUILD_DIR)/test-clock
SYSTEM_METRICS_TEST_TARGET := $(BUILD_DIR)/test-system-metrics
LAUNCHER_LAYOUT_TEST_TARGET := $(BUILD_DIR)/test-launcher-layout

.PHONY: all clean test strict sdk ui i18n integration-test lvgl lvgl-probe

all: $(TARGET) $(LVGL_PACKAGE_TARGET)

sdk:
	$(MAKE) -C $(SDK_DIR) build/libmsys-mipc.a

ui:
	$(MAKE) -C $(UI_DIR) build/libmsys-ui-lvgl.a

i18n:
	PYTHONPATH=$(SDK_DIR) python3 -m msys_sdk.i18n_c files/share/i18n/catalog.json generated/shell_catalog.h --symbol shell_catalog

$(TARGET): $(OBJECTS) | $(BIN_DIR) sdk
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

$(UI_LIBRARY):
	$(MAKE) -C $(UI_DIR) build/libmsys-ui-lvgl.a

$(BUILD_DIR)/lvgl/%.o: src/%.c $(UI_LIBRARY) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LVGL_CPPFLAGS) $(CFLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(LVGL_TARGET): $(LVGL_OBJECTS) $(UI_LIBRARY) | $(BIN_DIR) sdk
	$(CC) $(CFLAGS) -Wl,--gc-sections $(LVGL_OBJECTS) -o $@ \
		$(SDK_DIR)/build/libmsys-mipc.a $(UI_LIBRARY) -lX11 -ldl -lm

$(LVGL_PACKAGE_TARGET): $(LVGL_TARGET) $(LVGL_UI_FILES)
	@mkdir -p $(@D) files/share/licenses/lvgl files/share/licenses/msys-ui-lvgl
	cp $(LVGL_TARGET) $@
	cp $(UI_DIR)/vendor/lvgl/LICENCE.txt $(UI_DIR)/vendor/lvgl/COPYRIGHTS.md \
		files/share/licenses/lvgl/
	cp $(UI_DIR)/LICENSE files/share/licenses/msys-ui-lvgl/

lvgl: $(LVGL_PACKAGE_TARGET)

lvgl-probe: $(LVGL_PACKAGE_TARGET)
	tests/probe_lvgl_shell.sh

$(BUILD_DIR)/main.o: src/main.c generated/shell_catalog.h \
	include/msys_shell_native/model.h include/msys_shell_native/catalog.h \
	include/msys_shell_native/preferences.h include/msys_shell_native/image.h \
	include/msys_shell_native/notification.h include/msys_shell_native/clock.h \
	include/msys_shell_native/system_metrics.h \
	include/msys_shell_native/launcher_layout.h \
	$(SDK_DIR)/include/msys/mipc.h $(SDK_DIR)/include/msys/i18n.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/model.o: src/model.c include/msys_shell_native/model.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/catalog.o: src/catalog.c include/msys_shell_native/catalog.h \
	$(SDK_DIR)/include/msys/mipc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/preferences.o: src/preferences.c include/msys_shell_native/preferences.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/image.o: src/image.c include/msys_shell_native/image.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/notification.o: src/notification.c \
	include/msys_shell_native/notification.h $(SDK_DIR)/include/msys/mipc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/clock.o: src/clock.c include/msys_shell_native/clock.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/system_metrics.o: src/system_metrics.c \
	include/msys_shell_native/system_metrics.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/launcher_layout.o: src/launcher_layout.c \
	include/msys_shell_native/launcher_layout.h include/msys_shell_native/catalog.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): tests/test_model.c src/model.c include/msys_shell_native/model.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_model.c src/model.c -o $@

$(CATALOG_TEST_TARGET): tests/test_catalog.c src/catalog.c include/msys_shell_native/catalog.h | $(BUILD_DIR) sdk
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_catalog.c src/catalog.c -o $@ $(SDK_DIR)/build/libmsys-mipc.a

$(PREFERENCES_TEST_TARGET): tests/test_preferences.c src/preferences.c include/msys_shell_native/preferences.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_preferences.c src/preferences.c -o $@

$(IMAGE_TEST_TARGET): tests/test_image.c src/image.c include/msys_shell_native/image.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_image.c src/image.c -o $@ -lX11

$(I18N_TEST_TARGET): tests/test_i18n.c generated/shell_catalog.h | $(BUILD_DIR) sdk
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_i18n.c -o $@ $(SDK_DIR)/build/libmsys-mipc.a

$(NOTIFICATION_TEST_TARGET): tests/test_notification.c src/notification.c \
	include/msys_shell_native/notification.h | $(BUILD_DIR) sdk
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_notification.c src/notification.c \
		-o $@ $(SDK_DIR)/build/libmsys-mipc.a

$(CLOCK_TEST_TARGET): tests/test_clock.c src/clock.c \
	include/msys_shell_native/clock.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_clock.c src/clock.c -o $@

$(SYSTEM_METRICS_TEST_TARGET): tests/test_system_metrics.c src/system_metrics.c \
	include/msys_shell_native/system_metrics.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_system_metrics.c \
		src/system_metrics.c -o $@

$(LAUNCHER_LAYOUT_TEST_TARGET): tests/test_launcher_layout.c src/launcher_layout.c \
	include/msys_shell_native/launcher_layout.h include/msys_shell_native/catalog.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_launcher_layout.c \
		src/launcher_layout.c -o $@

test: $(TEST_TARGET) $(CATALOG_TEST_TARGET) $(PREFERENCES_TEST_TARGET) \
	$(IMAGE_TEST_TARGET) $(I18N_TEST_TARGET) $(NOTIFICATION_TEST_TARGET) \
	$(CLOCK_TEST_TARGET) $(SYSTEM_METRICS_TEST_TARGET) \
	$(LAUNCHER_LAYOUT_TEST_TARGET)
	$(TEST_TARGET)
	$(CATALOG_TEST_TARGET)
	$(PREFERENCES_TEST_TARGET)
	$(IMAGE_TEST_TARGET)
	$(I18N_TEST_TARGET)
	$(NOTIFICATION_TEST_TARGET)
	$(CLOCK_TEST_TARGET)
	$(SYSTEM_METRICS_TEST_TARGET)
	$(LAUNCHER_LAYOUT_TEST_TARGET)

strict:
	$(MAKE) clean
	$(MAKE) all test

integration-test: all
	tests/test_mobile_x11_runtime.sh

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LVGL_PACKAGE_TARGET) \
		files/share/licenses/lvgl files/share/licenses/msys-ui-lvgl
