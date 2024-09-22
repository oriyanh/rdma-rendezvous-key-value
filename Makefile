# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -O0

# Libraries to link
LIBS = -libverbs -lpthread

# Source files
SRC = main.c server.c client.c common/connection.c client_internals/client_requests_management.c server_internals/db.c server_internals/server_requests_management.c tests/client_tests.c tests/tests_common.c common/buffers_management.c

# Object files (replace .c with .o)
OBJ = $(SRC:.c=.o)

# Header files
HEADERS = server.h client.h common/constants.h common/connection.h client_internals/client_requests_management.h server_internals/db.h server_internals/server_requests_management.h external_libs/uthash.h external_libs/utlist.h  tests/client_tests.h tests/tests_common.h common/buffers_management.h

# Output executable
TARGET = server

# Symlink name
SYMLINK = client

# Default target: build the executable and create the symlink
all: $(TARGET) symlink

# Link object files to create the executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

# Create the symlink if it doesn't already exist
symlink:
	@if [ ! -L $(SYMLINK) ]; then ln -s $(TARGET) $(SYMLINK); fi

# Compile source files into object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the build files and the symlink
clean:
	rm -f $(OBJ) $(TARGET) $(SYMLINK)

# Rebuild the project
rebuild: clean all

.PHONY: all clean rebuild symlink
