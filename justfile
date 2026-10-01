build:
    cp ./slop/photo.jpeg ./build/
    cmake -S . -B build
    cmake --build build
