CC = gcc
# Installation prefixes (overridable)
PREFIX ?= /usr/local
DATADIR ?= $(PREFIX)/share
BINDIR ?= $(PREFIX)/bin
APPDATADIR ?= $(DATADIR)/applications
SCHEMADIR ?= $(DATADIR)/glib-2.0/schemas
# Keep current icon location; change if integrating with icon themes later
ICONDIR ?= $(DATADIR)/gtktext/icons

CFLAGS = $(shell pkg-config --cflags gtk4 libadwaita-1 libcmark) -I/usr/include -Wall -Wextra -g -Iinclude \
         -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
         -DPKGDATADIR="\"$(DATADIR)/gtktext\"" -DGETTEXT_PACKAGE="\"gtktext\"" -DLOCALEDIR="\"$(DATADIR)/locale\""
LDFLAGS = $(shell pkg-config --libs gtk4 libadwaita-1 libcmark) \
          -Wl,-z,relro -Wl,-z,now -pie

# Optional libsoup-3.0 for HTTP(S) image loading
SOUP_CFLAGS := $(shell pkg-config --cflags libsoup-3.0 2>/dev/null)
SOUP_LIBS   := $(shell pkg-config --libs libsoup-3.0 2>/dev/null)
ifeq ($(strip $(SOUP_CFLAGS)),)
  $(info libsoup-3.0 not found; HTTP image loading disabled)
else
  CFLAGS += $(SOUP_CFLAGS) -DHAVE_LIBSOUP=1
  LDFLAGS += $(SOUP_LIBS)
endif

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))
TEST_SRC = $(wildcard $(TEST_DIR)/*.c)
TEST_BIN = $(patsubst $(TEST_DIR)/%.c, $(TEST_DIR)/bin/%, $(TEST_SRC))

TARGET = $(BIN_DIR)/gtktext

all: directories $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: directories-test $(TEST_BIN)
	@for test in $(TEST_BIN); do \
		echo "Running $$test..."; \
		$$test; \
	done

$(TEST_DIR)/bin/%: $(TEST_DIR)/%.c $(filter-out $(OBJ_DIR)/main.o, $(OBJ))
	@mkdir -p $(TEST_DIR)/bin
	$(CC) $(CFLAGS) $< $(filter-out $(OBJ_DIR)/main.o, $(OBJ)) -o $@ $(LDFLAGS)

directories:
	mkdir -p $(OBJ_DIR) $(BIN_DIR)

directories-test:
	mkdir -p $(TEST_DIR)/bin

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(TEST_DIR)/bin

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/gtktext
	# Desktop entry
	install -Dm644 data/gtktext.desktop $(DESTDIR)$(APPDATADIR)/gtktext.desktop
	# Icons
	install -d $(DESTDIR)$(ICONDIR)
	cp -r data/icons/* $(DESTDIR)$(ICONDIR)/
	# UI files
	install -d $(DESTDIR)$(DATADIR)/gtktext/ui
	install -Dm644 ui/main_window.ui $(DESTDIR)$(DATADIR)/gtktext/ui/main_window.ui
	# GSettings schema
	install -Dm644 data/org.gtk.gtktext.gschema.xml \
		$(DESTDIR)$(SCHEMADIR)/org.gtk.gtktext.gschema.xml
	glib-compile-schemas $(DESTDIR)$(SCHEMADIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/gtktext
	rm -f $(DESTDIR)$(APPDATADIR)/gtktext.desktop
	rm -rf $(DESTDIR)$(ICONDIR)
	rm -f $(DESTDIR)$(SCHEMADIR)/org.gtk.gtktext.gschema.xml

format:
	find $(SRC_DIR) $(TEST_DIR) include -name "*.c" -o -name "*.h" | xargs clang-format -i -style=file

.PHONY: all clean install uninstall directories directories-test test format
