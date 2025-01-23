mkdir build
cd build
cmake .. -DBUILD_LWIPTEST=ON 
cmake --build .
cd ..
./build/test/lwiptest

