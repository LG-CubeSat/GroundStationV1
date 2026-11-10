build:
    mkdir -p build
    cp ./slop/joi.jpg ./build
    cp ./slop/john.jpg ./build
    cmake -S . -B build \
      -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
      -DOPENSSL_USE_STATIC_LIBS=ON \
      -DCURL_USE_LIBSSH2=OFF \
      -DCURL_USE_LIBSSH=OFF \
      -DCURL_DISABLE_LDAP=ON \
      -DCURL_USE_LIBPSL=OFF
    cmake --build build
