#! /bin/bash
wget -nc -O build/libsodium.tar.gz "https://download.libsodium.org/libsodium/releases/libsodium-1.0.18-stable.tar.gz"
cd build || exit
tar -xzf libsodium.tar.gz
printf "#define CONFIGURED 1\n#define NATIVE_LITTLE_ENDIAN\n" | cat - libsodium-stable/src/libsodium/include/sodium/private/common.h >libsodium-stable/src/libsodium/include/sodium/private/common.h.new
mv libsodium-stable/src/libsodium/include/sodium/private/common.h.new libsodium-stable/src/libsodium/include/sodium/private/common.h
printf "#define SODIUM_STATIC\n" | cat - libsodium-stable/src/libsodium/include/sodium/export.h >libsodium-stable/src/libsodium/include/sodium/export.h.new
mv libsodium-stable/src/libsodium/include/sodium/export.h.new libsodium-stable/src/libsodium/include/sodium/export.h
printf "#define abort __builtin_trap\n" | cat - libsodium-stable/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c >libsodium-stable/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c.new
mv libsodium-stable/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c.new libsodium-stable/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c
printf "#define abort __builtin_trap\n" | cat - libsodium-stable/src/libsodium/sodium/core.c >libsodium-stable/src/libsodium/sodium/core.c.new
mv libsodium-stable/src/libsodium/sodium/core.c.new libsodium-stable/src/libsodium/sodium/core.c
cd ..
