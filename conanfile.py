from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout


class TaskPoolConan(ConanFile):
    name = "task_pool"
    version = "0.0"

    # Optional metadata
    license = "MIT"
    author = "Benny Edlund benny.edlund@gmail.com"
    url = "https://github.com/benny-edlund/task-pool"
    description = "A portable task orient thread pool library compatible with C++14 with support for custom allocators, cooperative cancellation and more"
    topics = ("threadpool", "thread", "asynchronous", "concurrency", "task", "c++")

    requires = ("catch2/2.13.9",)
    generators = ("cmake_find_package_multi",)
    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "conanfile.py", "lib/*", "configured_files/*", "test/*"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure({'ENABLE_DEVELOPER_MODE:BOOL':'OFF'})
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["task_pool"]
