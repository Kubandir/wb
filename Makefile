CXX = g++
CXXFLAGS_COMMON = -Iinclude -lncursesw -std=c++17
CXXFLAGS_DEV = -O0 -g $(CXXFLAGS_COMMON)
CXXFLAGS_RELEASE = -O3 -DNDEBUG $(CXXFLAGS_COMMON)

SRC = src/main.cpp src/editor.cpp src/render.cpp src/input.cpp src/file_ops.cpp
BIN_DIR = bin
TARGET = $(BIN_DIR)/wb
INSTALL_DIR = /usr/local/bin

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

dev: $(BIN_DIR)
	$(CXX) $(CXXFLAGS_DEV) $(SRC) -o $(TARGET)

release: $(BIN_DIR)
	$(CXX) $(CXXFLAGS_RELEASE) $(SRC) -o $(TARGET)

install: release
	install -m 755 $(TARGET) $(INSTALL_DIR)/wb

uninstall:
	rm -f $(INSTALL_DIR)/wb

clean:
	rm -rf $(BIN_DIR)

.PHONY: dev release install uninstall clean
