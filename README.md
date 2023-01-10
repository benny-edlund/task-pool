# task-pool


[![ci](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/benny-edlund/task-pool/branch/main/graph/badge.svg?token=ONIOP80W68)](https://codecov.io/g/benny-edlund/task-pool)
[![CodeQL](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml)
## About the library

The goal of task_pool was to write a minimal yet capable thread-pool library in C++14 with support for some non obvious
features like support for allocators and intuitive support for cooperative cancelation.

### Basic use
```cpp
be::task_pool pool(4);
auto task = []( std::string x) { std::cerr<< "hello " << x << '\n' ); };
pool.submit( task, std::string("world") );
pool.wait_for_tasks();
```
Here we first create a pool with four threads then create lambda to use as a task and submit it to the pool for processing.

Our task takes a string argument to print so we must submit this argument along with our task. Input arguments are forwarded
to tasks using a [bind expression](https://en.cppreference.com/w/cpp/utility/functional/bind), in this case something like
```cpp 
std::bind( task, std::forward<std::string&&>(x) );
```

Finally we wait on all tasks to complete using task_pool\:\:wait_for_tasks().

Another option to wait for completion of our task would have been to take a future from the submit function and wait using
the [std\:\:future](https://en.cppreference.com/w/cpp/thread/future) api.

```cpp
auto future = pool.submit(task, std::string("world") );
future.wait();
```

For tasks that do not return values is legal disgard the returned future from task_pool::submit however if our task actually
return a value we must capture the future or the expression will not compile

```cpp
auto task = []( int x ) { return x; };
// not capturing result here would be a compiler error
auto result = pool.submit( task, 42 ); 
std::cerr << "The anwser is " << result.get();
```

### Task types

Most callable value types can be used as a task. Free functions, member functions, 

```cpp
struct work_data { int value; }; 

// Free functions that expect values
void work_item( work_data data, be::stop_token abort ) {
    if ( !abort ) {
        std::cerr<<"The awnser is "<< data.value<<'\n';
    }
}
pool.submit(&work_item,work_data{42});

// Objects with templated call operators and auto return types
struct work
{
    template< typename WorkType>
    auto operator()( WorkType data ) {
        return data.value;
    }
};
auto result = pool.submit( work{}, work_data{ 42 } );
@endcode

// Member functions can also be bound if their owning instance is
// rechable at execution time

class special_work
{
    void do_something();
public:
    void run(be::stop_token abort) { while( !abort ) do_something(); } 
};

special_work task;
pool.submit(&special_work::run,&task);
```

### Cooperative cancellation

The library also supports a simple form of cooperative cancellation by allowingsubmitted tasks that take a special type, 
the stop_token, to check if the owning pool needs finish work early. 

Tasks may use the token to abort active work allowing the pool to shutdown

```cpp
be::task_pool pool;
pool.submit( []( be::stop_token abort ) { 
    auto then = std::chrono::now()+10m;
    while( !abort && std::chrono::now()<then ) {
        std::this_thread::sleep_for(1ms);
    }
});
```
The pool destructor will ask running tasks to cancel and if our task did not check this token it would need to wait the 
full 10 minutes it takes for the task to finish its work


### Using allocators

Tasks submitted to task_pools require intermediate storage on the heap and this can become a limiting
factor to applications. To help task_pool supports using custom allocators for storing tasks until executed
as well as the shared state of the std::futures used.

```cpp
be::task_pool pool;
pool.submit( std::allocator_arg_t{}, allocator, &fun );
```

std\:\:allocator_arg_t is used to disambiguate the overloads to task_pool::submit
