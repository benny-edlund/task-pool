# Building `task_pool`

The project is built with [`cmake`](https://cmake.org/install) and it uses [`conan`](https://docs.conan.io/en/latest/installation.html) for dependencies. The library itself does not require any external dependencies but the test suite for example uses [catch2](https://github.com/catchorg/Catch2) and there may be example applications added in the future that require addition libraries to be built.

The project also uses the cmake module  [project_options](https://github.com/aminya/project_options) from `Amin Yahyaabadi` to provide many useful build configurations. Please review their documentation for details on specific configurations available.

A standard release configuration with debug symbols may be produced in the following way
```bash
cmake -S . -B ./build -G "Ninja" -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo
cmake --build ./build
```

&nbsp;
### Developer mode
The default build (referred to a developer mode in `project_options`) will be a Debug build with sanitizers enabled built with `-Werrors`
```bash
cmake -S . -B ./build -G "Ninja"
cmake --build ./build
```

&nbsp;

If desired developer mode may be disabled for a more convential Debug library build
```bash
cmake -S . -B ./build -G "Ninja" -DENABLE_DEVELOPER_MODE:BOOL=OFF
cmake --build ./build
```