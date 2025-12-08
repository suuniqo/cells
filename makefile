CFLAGS := -Wall -Wextra -Werror -pedantic
CC := gcc
NAME := cells
DIR_SRC := src
DIR_BIN := bin
DIR_OBJ := build

DIRS_SRC = $(shell find $(DIR_SRC)/ -type d)
DIRS_OBJ = $(patsubst $(DIR_SRC)/%, $(DIR_OBJ)/%, $(DIRS_SRC))

SRCS = $(shell find $(DIR_SRC) -name *.c)
OBJS = $(patsubst $(DIR_SRC)/%.c, $(DIR_OBJ)/%.o, $(SRCS))

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $(DIR_BIN)/$@

$(DIR_OBJ)/%.o: $(DIR_SRC)/%.c | dir
	$(CC) $(CFLAGS) -c $< -o $@

dir:
	mkdir -p $(DIR_BIN) $(DIRS_OBJ)

run: $(NAME)
	$(DIR_BIN)/$(NAME)

clean:
	rm -rf $(DIR_OBJ)

veryclean:
	rm -rf $(DIR_OBJ)
	rm -rf $(DIR_BIN)
	rm compile_commands.json
