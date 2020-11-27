ERROR_FLAGS=-Werror -ferror-limit=0 -Wall -Wextra -pedantic-errors -Wno-c++98-compat -Wno-c++-compat -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-unused-variable -Wno-unused-parameter
# Disable -Werror if you can't compile due to differing compiler versions.
COMMON_FLAGS=-std=gnu++17 -O0 -g -fno-exceptions
# Add -fsanitize=address to check for memory issues
# Optimizations will happen later
LINK_OPTIONS=-lsodium

all:
	clang++ $(ERROR_FLAGS) $(COMMON_FLAGS) src/main.cpp -o build/bytefall $(LINK_OPTIONS)

clean:
	rm -r build/*
