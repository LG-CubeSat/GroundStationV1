build:
    mkdir -p build
    cp ./slop/joi.jpg ./build
    cp ./slop/john.jpg ./build
    cmake -S . -B build
    cmake --build build

test:
    socat -d -d pty,rawer,link=/tmp/gs-tx pty,rawer,link=/tmp/gs-rx & 
    E22_UART_DEVICE=/tmp/gs-rx ./build/GSMain &
    ./build/GSEyecandy &
    python3 src/test.py --port /tmp/gs-tx --count 0 &

stop-test:
    pkill GSMain
    pkill GSEyecandy
    pkill socat
