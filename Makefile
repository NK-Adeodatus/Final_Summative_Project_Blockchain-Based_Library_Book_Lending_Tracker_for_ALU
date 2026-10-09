CC=gcc
CFLAGS=-Wall -Wextra -O2 -D__USE_MINGW_ANSI_STDIO=1

SRC_DIR=src
INC_DIR=include
OBJ_DIR=obj

# List all source files
SRCS=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Executable name
TARGET=alu_blockchain_library

LIBS=-lssl -lcrypto

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe
