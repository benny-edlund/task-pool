# task-pool


[![ci](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/benny-edlund/task-pool/branch/main/graph/badge.svg?token=ONIOP80W68)](https://app.codecov.io/gh/benny-edlund/task-pool)
[![CodeQL](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml)

&nbsp;  
## About the library

The goal of task_pool was to write a minimal yet useful thread-pool library in C++14 with support for some non obvious features like allocators, using futures as task parameters and intuitive support for cooperative cancelation.

The library tries to be pragmatic rather then generic and provides some practial solutions to problems typically faced when developing asynchronous applications.

&nbsp;
## Basic use
```cpp
be::task_pool pool;
auto task = []() { std::cerr<< "Hello World! " << x << '\n' ); };
pool.submit( task );
pool.wait_for_tasks();
```
Here we first create a default constructed pool object. A lambda is then declared and passed by reference to `be::task_pool:submit` as a task to execute in the thread pool. 

Finally we use `be::task_pool::wait_for_tasks` to ensure all tasks submitted to the pool until that point are completed before continuing.

&nbsp;
## Initialization and thread count 
  
Default constructed task_pool objects will hold the amount of threads returns by [std::thread::hardware_concurrency](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency) which would typically return the amount of concurrent task a system can support in hardware.

To create a pool with a different amount of threads you may provide the desired amount of threads to the task_pool constructor.

```cpp
be::task_pool pool(1);
```

Task pool thread counts may be changed during the lifetime of the pool object but not while the pool is executing tasks. To query the amount of threads currently used by a pool call `be::task_pool::get_thread_count` and to change the thread count call `be::task_pool::reset` with your desired amount of threads.

```cpp
be::task_pool pool(1);
// some time later
if ( pool.get_thread_count() < 8 ) {
    pool.reset(8);
}
```
`be::task_pool::reset` will block threads attempting to submit new tasks while the reset is waiting for current tasks are finishing so should not be used during time sensitive program sections.

&nbsp;
## Task types
Most callable value types can be used as a task. Free functions, member functions, lambdas and std::function objects are all options as long as relevant life time rules are observed.

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

&nbsp;
## Return values

Task functions may return values to the submitter as every call to `be::task_pool::submit` returns a [`std::future`](https://en.cppreference.com/w/cpp/thread/future) that can be used to get the result value when its available as the task function returns.

```cpp
be::task_pool pool;
auto task = []() { return 42; };
auto result = pool.submit( task );
auto the_awnser = result.get();
```
When using `task_pool` it is only required to capture the futures of tasks that return values. This is different to other standard C++ APIs like std::async where the caller must capture the future into a value or it will execute in the calling thread. 

However should you capture a future from some task of interest that does not return any value it provides another obvious option to wait for its completion using [`std::future::wait_for`](https://en.cppreference.com/w/cpp/thread/future/wait_for) which is typically well optimized by compilers.

```cpp
be::task_pool pool;
auto task = []() { return 42; };
auto future = pool.submit(task);
while ( future.wait_for(0s) != std::future_status::ready) {
    do_other_things()
}
```
Failing to capture the future of a task that does return a value will result in a compiler error.

```cpp
auto task = []( int x ) { return x; };
pool.submit( task, 42 ); // this will not compile
```
&nbsp;
## Input arguments

Its possible to pass initial arguments to tasks when submitting them to the pool.

```cpp
be::task_pool pool;
auto task = []( std::string x ){ std::cerr<<"Hello "<<x;};
pool.submit(task, std::string( "World" ) );
```

Arguments are passed to the submit function after the task function and are captured for the future task execution by forwarding into a [bind expression](https://en.cppreference.com/w/cpp/utility/functional/bind). [^1]

It is also possible to pass values to functions using futures. Initially this might seem obvious and its easy to see how one may take a future as a function argument however `be::task_pool::submit` has the additional ability to detect tasks that require futures and placed them in a holding area while dependencies finish executing without blocking any additional threads. 


```cpp
namespace some_api 
{
    std::future<LargeData> generate_data_async();
    void process_data( LargeData );
}

be::task_pool pool;
auto future_data = generate_data_async();
auto result = pool.submit(&some_api::process_data,std::move(future_data));

while( result.wait_for(0s) != std::future_status::ready) {
    do_important_things();
}
```
In this slightly contrived exampled some external api is used to generate and process `LargeData` objects supposedly in an asynchronous way that is out of our control. 

Instead of using an entire thread to wait for `future_data` to be ready we can immediately submit this future with the data processor to the pool and it will use the wait time in the pool to check when the future is ready and start the processing function as promptly as possible.

This works for all future-like objects. [^2]

&nbsp;
## Cooperative cancellation

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

&nbsp;
## Using allocators

Tasks submitted to task_pools require intermediate storage on the heap and this can become a limiting
factor to applications. To help task_pool supports using custom allocators for storing tasks until executed
as well as the shared state of the std::futures used.

```cpp
be::task_pool pool;
pool.submit( std::allocator_arg_t{}, allocator, &fun );
```

`std::allocator_arg_t` is used to disambiguate the overloads to `be::task_pool::submit`

### Notes:
---------
[^1]: Futher improvents needed here to reduce copies and temporaries. Currently the most effcient way seems to be to take const reference in the task function and move/construct into the submit call. This will move into the bind expression and the function call will then reference out of this bind expresssion. Yes improvements are possible and will be done.

[^2] Future-like objects must implement `get`, `wait`, `wait_for`, `wait_until` to be considered future-like