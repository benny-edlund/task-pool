# Task pool examples
As most examples in tests and the documentation are rather contrived these examples here are provided to show task-pool workflows in real applications that actually do something.

## Building
First build task-pool and install to some temporary location

```bash
git clone https://github.com/benny-edlund/task-pool.git
cd task-pool
cmake -S . -B ./build -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build ./build
cmake --install ./build
```
Next build one or all the examples using this installed version.

The examples has some external dependencies, fmt, CLI11, boost (headers) and turbojpeg so conan may take a while to download and potentially compile the dependencies

```bash
cd examples
conan install . --output-folder=build
cmake -S . -B ./build -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build ./build
```


## collage
(macOS, Linux)

This example generates an image collage by querying Wikipedia using curl in multiple threads. It has an intentially poor design that requires jobs to handle failues and get retried.
```bash
./{build_folder}/collage/example_collage ./output.jpg
```

## webserver
(macOS, Linux)
This example contains a hello-world style webserver. It feature building a pipeline with the pipe api to handle requests without sharing any state with the main application.
```bash
./{build_folder}/webserver/example_http 127.0.0.1 -p 8081
```


## image_processing
(Windows, macOS, Linux)

This example generates random images and builds a processing pipeline from a polymorphic processor class then compresses the resulting image to jpeg and saves it to disk. The pipeline is build mapping member function pointer onto base instances in the standard api.
```bash
./{build_folder}/image_processing/example_img ./test.jpg
```

