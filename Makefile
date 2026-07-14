# Terminal Spreadsheet — Makefile
CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -pedantic -O2
LDFLAGS  = -lncursesw -lm

SRC_DIR  = src
OBJ_DIR  = obj
SRC      = $(SRC_DIR)/main.c \
           $(SRC_DIR)/grid.c \
           $(SRC_DIR)/render.c \
           $(SRC_DIR)/input.c \
           $(SRC_DIR)/edit.c \
           $(SRC_DIR)/formula.c \
           $(SRC_DIR)/dependency.c \
           $(SRC_DIR)/config.c \
           $(SRC_DIR)/fileio.c
OBJ      = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))
TARGET   = spreadsheet

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/grid.h $(SRC_DIR)/config.h $(SRC_DIR)/fileio.h $(SRC_DIR)/utf8.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
