build:
    mkdir -p build
    cp ./slop/joi.jpg ./build
    cp ./slop/john.jpg ./build
    cmake -S . -B build \
      -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
      -DCURL_USE_LIBPSL=OFF
    cmake --build build
