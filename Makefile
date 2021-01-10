ERROR_FLAGS=-Werror -ferror-limit=0 -Wall -Wextra -pedantic-errors -Wno-c++98-compat -Wno-c++-compat -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-unused-variable -Wno-unused-parameter
# Disable -Werror if you can't compile due to differing compiler versions.
COMMON_FLAGS=-std=gnu++17 -Ofast -fno-exceptions #-g
# Add -fsanitize=address to check for memory issues
# Optimizations will happen later
LINK_OPTIONS=-lsodium
WASM_FLAGS=--no-standard-libraries -mreference-types -fvisibility=hidden -ffunction-sections -fdata-sections -flto -Wl,--no-entry,--strip-all,--export-dynamic,--gc-sections,--allow-undefined,--initial-memory=131072,--lto-O3,-O3
# Add -msimd128 if CF Workers adds it
WASM_INCLUDES=-Ibuild/libsodium-stable/src/libsodium/include -Ibuild/libsodium-stable/src/libsodium/include/sodium -Iworker/waterfall/minilib -I/usr/local/Cellar/libsodium/1.0.18_1/include
# Extra memory can be done like -Wl,--initial-memory=131072
WASM_EXTRAS=build/libsodium-stable/src/libsodium/crypto_sign/crypto_sign.c build/libsodium-stable/src/libsodium/crypto_sign/ed25519/ref10/open.c build/libsodium-stable/src/libsodium/crypto_sign/ed25519/ref10/sign.c build/libsodium-stable/src/libsodium/crypto_hash/sha512/cp/hash_sha512_cp.c build/libsodium-stable/src/libsodium/crypto_verify/sodium/verify.c build/libsodium-stable/src/libsodium/crypto_pwhash/argon2/argon2-core.c build/libsodium-stable/src/libsodium/crypto_generichash/blake2b/ref/generichash_blake2b.c build/libsodium-stable/src/libsodium/crypto_generichash/blake2b/ref/blake2b-ref.c build/libsodium-stable/src/libsodium/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c build/libsodium-stable/src/libsodium/crypto_scalarmult/curve25519/scalarmult_curve25519.c build/libsodium-stable/src/libsodium/crypto_stream/chacha20/stream_chacha20.c build/libsodium-stable/src/libsodium/crypto_stream/salsa20/stream_salsa20.c build/libsodium-stable/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c build/libsodium-stable/src/libsodium/randombytes/randombytes.c build/libsodium-stable/src/libsodium/sodium/core.c build/libsodium-stable/src/libsodium/sodium/runtime.c build/libsodium-stable/src/libsodium/sodium/utils.c worker/waterfall/minilib/string.cpp

all:
	clang++ $(ERROR_FLAGS) $(COMMON_FLAGS) src/main.cpp -o build/bytefall $(LINK_OPTIONS)

wasm:
	bash GetSodium.sh
	/usr/local/opt/llvm/bin/clang++ --target=wasm32 $(WASM_FLAGS) $(COMMON_FLAGS) worker/waterfall/main.cpp $(WASM_EXTRAS) -o build/bytefall.wasm $(WASM_INCLUDES)

clean:
	rm -r build/*
