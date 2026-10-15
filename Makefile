CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
INC_DIR = include
SRC_DIR = src
OBJ_DIR = obj

# Default libraries
LIBS = -lssl -lcrypto
LDFLAGS =
EXE_EXT =

# Operating system detection
ifeq ($(OS),Windows_NT)
    # Native Windows (MinGW / MSYS2)
    EXE_EXT = .exe
    CFLAGS += -D__USE_MINGW_ANSI_STDIO=1
    LIBS += -lws2_32 -lgdi32 -lcrypt32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        # macOS with Homebrew OpenSSL
        OPENSSL_PREFIX ?= $(shell brew --prefix openssl 2>/dev/null || echo /opt/homebrew/opt/openssl)
        ifneq ($(wildcard $(OPENSSL_PREFIX)/include),)
            CFLAGS += -I$(OPENSSL_PREFIX)/include
            LDFLAGS += -L$(OPENSSL_PREFIX)/lib
        endif
    else ifeq ($(UNAME_S),Linux)
        # Standard Linux / Ubuntu / WSL / Debian
        PKG_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
        PKG_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
        ifneq ($(PKG_CFLAGS),)
            CFLAGS += $(PKG_CFLAGS)
        endif
        ifneq ($(PKG_LIBS),)
            LIBS := $(PKG_LIBS)
        endif
    endif
endif

# Target executable
TARGET = alu_blockchain_library$(EXE_EXT)

# Sources and objects
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

.PHONY: all clean run test

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

run: all
	./$(TARGET)

test: all
	bash test/run_tests.sh

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe chain_data
