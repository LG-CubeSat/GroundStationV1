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

test:
    socat -d -d pty,rawer,link=/tmp/gs-tx pty,rawer,link=/tmp/gs-rx &
    until [ -e /tmp/gs-rx ]; do sleep 0.1; done
    E22_UART_DEVICE=/tmp/gs-rx ./build/GSMain &
    ./build/GSEyecandy &
    python3 src/test.py --port /tmp/gs-tx --count 0 &

stop-test:
    pkill GSMain
    pkill GSEyecandy
    pkill socat
