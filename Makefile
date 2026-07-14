CC ?= cc
AR ?= ar
SDK_DIR ?= ../msys-sdk
BUILD_DIR ?= build
BIN_DIR ?= bin

CPPFLAGS += -Iinclude -Igenerated -I$(SDK_DIR)/include
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDLIBS += $(SDK_DIR)/build/libmsys-mipc.a -lX11 -ldl

SOURCES := src/main.c src/model.c src/catalog.c src/preferences.c src/image.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
TARGET := $(BIN_DIR)/msys-shell-native
TEST_TARGET := $(BUILD_DIR)/test-model
CATALOG_TEST_TARGET := $(BUILD_DIR)/test-catalog
PREFERENCES_TEST_TARGET := $(BUILD_DIR)/test-preferences
IMAGE_TEST_TARGET := $(BUILD_DIR)/test-image
I18N_TEST_TARGET := $(BUILD_DIR)/test-i18n

.PHONY: all clean test strict sdk i18n integration-test

all: $(TARGET)

sdk:
	$(MAKE) -C $(SDK_DIR) build/libmsys-mipc.a

i18n:
	PYTHONPATH=$(SDK_DIR) python3 -m msys_sdk.i18n_c files/share/i18n/catalog.json generated/shell_catalog.h --symbol shell_catalog

$(TARGET): $(OBJECTS) | $(BIN_DIR) sdk
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/main.o: src/main.c generated/shell_catalog.h \
	include/msys_shell_native/model.h include/msys_shell_native/catalog.h \
	include/msys_shell_native/preferences.h include/msys_shell_native/image.h \
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

test: $(TEST_TARGET) $(CATALOG_TEST_TARGET) $(PREFERENCES_TEST_TARGET) \
	$(IMAGE_TEST_TARGET) $(I18N_TEST_TARGET)
	$(TEST_TARGET)
	$(CATALOG_TEST_TARGET)
	$(PREFERENCES_TEST_TARGET)
	$(IMAGE_TEST_TARGET)
	$(I18N_TEST_TARGET)

strict:
	$(MAKE) clean
	$(MAKE) all test

integration-test: all
	tests/test_mobile_x11_runtime.sh

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
