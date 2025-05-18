CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk4 libadwaita-1 libcmark) -I/usr/include -Wall -Wextra -g -Iinclude
LDFLAGS = $(shell pkg-config --libs gtk4 libadwaita-1 libcmark)

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
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(TARGET) $(DESTDIR)/usr/local/bin/
	mkdir -p $(DESTDIR)/usr/local/share/gtktext/icons
	cp -r data/icons/* $(DESTDIR)/usr/local/share/gtktext/icons/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/gtktext
	rm -rf $(DESTDIR)/usr/local/share/gtktext

format:
	find $(SRC_DIR) $(TEST_DIR) include -name "*.c" -o -name "*.h" | xargs clang-format -i -style=file

.PHONY: all clean install uninstall directories directories-test test format
