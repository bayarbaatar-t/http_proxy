EXE=htproxy

# auto-detect root vs src/
SRCDIR := $(if $(wildcard src),src,.)
INCDIR := $(if $(wildcard include),include,.)

SRC=$(wildcard $(SRCDIR)/*.c)
OBJ=$(SRC:.c=.o)
CC=cc
CFLAGS=-O3 -Wall -I$(INCDIR)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXE) $(OBJ)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXE)

format:
	clang-format -style=file -i $(SRCDIR)/*.c $(INCDIR)/*.h