# task-pool


[![ci](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/benny-edlund/task-pool/branch/main/graph/badge.svg?token=ONIOP80W68)](https://app.codecov.io/gh/benny-edlund/task-pool)
[![CodeQL](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml)
&nbsp;  
## About the library

The goal of task_pool was to write a minimal yet useful thread-pool library in C++14 with support for some non obvious features on a permissive license.

The library currently support lazy task arguments, cooperative cancellation and function composition with pipes. It also allows passing user defined allocators to manage the intermediate storage of task functions and arguments as well as pass through given allocators to task functions at runtime.

&nbsp;

### [Build instructions](docs/building.md)
Describes build the library from source and it's various configuration options.

&nbsp;  
### [Usage tutorial](docs/tutorial.md)
Comprehensive description on how to use the features of this library to build asynchronous applications.
