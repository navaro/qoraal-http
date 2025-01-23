mkdir build
cd build
cmake .. -DBUILD_LWIPTEST=ON -G "MinGW Makefiles" 
cmake --build .
cd ..
.\build\test\lwiptest
