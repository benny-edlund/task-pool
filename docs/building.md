# Building `task_pool`

The project is built with [`cmake`](https://cmake.org/install) and it uses [`conan`](https://docs.conan.io/en/latest/installation.html) for dependencies.

A standard release configuration may be produced in the following way
```bash
conan install . --output-folder=build --build=missing --settings=build_type=Release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```
&nbsp;
### Developer mode

```bash
conan install . --output-folder=build --build=missing --settings=build_type=Debug
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_DEVELOPER_MODE=ON -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
```

&nbsp;

If desired developer mode may be disabled for a more convential Debug library build
```bash
cmake -S . -B build  -DENABLE_DEVELOPER_MODE:BOOL=OFF
cmake --build build
```