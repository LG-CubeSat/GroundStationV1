build:
    mkdir -p build
    cp ./slop/joi.jpg ./build
    cp ./slop/john.jpg ./build
    cmake -S . -B build
    cmake --build build
