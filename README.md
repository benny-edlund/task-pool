# task-pool


[![ci](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/benny-edlund/task-pool/branch/main/graph/badge.svg?token=ONIOP80W68)](https://app.codecov.io/gh/benny-edlund/task-pool)
[![CodeQL](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/benny-edlund/task-pool/actions/workflows/codeql-analysis.yml)

&nbsp;  
## About the library

The goal of task_pool was to write a minimal yet useful thread-pool library in C++14 with support for some non obvious features like allocators, efficiently linking task using futures and intuitive support for cooperative cancellation.

The library tries to be pragmatic rather then generic and provides some practial solutions to problems typically faced when developing asynchronous applications.

&nbsp;
## Basic use
```cpp
be::task_pool pool;
auto task = []() { std::cerr<< "Hello World! " << x << '\n'; };
pool.submit( task );
pool.wait_for_tasks();
```
Here we first default construct a pool object. A lambda is then declared and passed by reference to `be::task_pool:submit` as a task to execute in the pool. 

Finally we use `be::task_pool::wait_for_tasks` to ensure all tasks submitted to the pool are completed before finishing.

&nbsp;

## Task types
Most callable value types can be used as a task. Free and member functions pointers can be passed by value. Callable objects such as lambdas and std::function as well as user defined types implementing an appropriate call operator can be passed both by value and by reference as long as relevant life time rules are observed.


&nbsp;
```cpp
struct work_data
{
    // lets imagine some large data structure here
};
work_data make_data();  
bool process_data( work_data );
```
Given some some API to create and process a dataset the most obvious way to work with it may be to use a lambda.
```cpp
auto result = pool.submit( []{ process_data( make_data() ); } );
```
The lambda is passed by value and stored in the `task_pool` executing the two function in sequence.

We can also pass the functions pointers themselves by value as tasks to execute.
```cpp
auto future = pool.submit( &make_data );
auto result = pool.submit( &process_data, std::move(future) );
```
Here we pass the function pointers by value directly to the thread pool capturing a future value from `make_data` and utilizing the `task_pool` to efficiently wait for this data to be available before processing it. 

More on this later...
&nbsp;

```cpp
struct base_processor
{
    virtual void run() = 0;
};

class processor : public base_processor
{
    void do_something_private( )
public:
    void run() override { 
        do_something_private(  ); 
    } 
};

auto work_a = std::make_unique<base_process>( new processor() );
auto work_b = std::make_unique<base_process>( new processor() );
auto done_a = pool.submit(&base_processor::run, work_a.get() );
auto done_b = pool.submit(&base_processor::run, work_b.get() );
```
Here we have a user defined type and we pass its `run` function pointer by value along with pointers to the instances we wish to execute the method on. With this work load we need to make sure that the tasks have completed before the work items are destroyed.
&nbsp;

We can of course also simply pass callable types by value just like lambdas, even when they have elaborate call operators.
```cpp

struct Data
{
    int value;
};
struct Work
{
    template< typename WorkType>
    auto operator()( WorkType data ) {
        return data.value;
    }
};
auto result = pool.submit( Work{}, Data{ 42 } );
```

Here `task_pool` with check that `work( Data )` is a valid function call prior to submitting and will generate compilation error in the case it is not.

&nbsp;
## Return values

As we have seen any task submitted to a pool returns a [`std::future`](https://en.cppreference.com/w/cpp/thread/future) that can be used to get the result value when its available as the task function returns.

```cpp
be::task_pool pool;
auto task = []() { return 42; };
auto result = pool.submit( task );
auto the_awnser = result.get();
```

When using `task_pool` it is only required to capture the futures of tasks that return values however using `std::future<void>` to check for task completions with its  [`std::future::wait_for`](https://en.cppreference.com/w/cpp/thread/future/wait_for) API is still recommended as it is typically well optimized by compilers.

```cpp
be::task_pool pool;
auto task = []() { return 42; };
auto future = pool.submit(task);
while ( future.wait_for(0s) != std::future_status::ready) {
    do_other_things()
}
```
Failing to capture the future of a task that does return a value will result in a compilation error.

```cpp
auto task = []( int x ) { return x; };
pool.submit( task, 42 ); // this will not compile
```

&nbsp;
## Input arguments

Its possible to pass arguments to tasks when submitting them to the pool. Required arguments for functions are simply passed to `submit` after the function itself.

```cpp
be::task_pool pool;
auto task = []( std::string x ){ std::cerr<<"Hello "<<x;};
pool.submit(task, std::string( "World" ) );
```

Since the actual invokation of our task is deferred we need our input data to be copied or moved into some kind of storage until the task is executed. The library currently does this by forwarding the task function and its arguments from `submit` into a [bind expression](https://en.cppreference.com/w/cpp/utility/functional/bind) [^1]

If the task function wants to take an argument by reference this may lead to counter intuitive results.

```cpp
void process_data( work_data& );
void do_work( work_data& data ) {
    be::task_pool pool;
    pool.submit(&process_data, data ); 
}
```
This example will compile however the task function will not operate on the work_data value referenced into the `do_work` function. This is because the underlying bind expression must copy the `work_data` value passed by reference to submit into the task storage and when executed the task will reference this data instead.

The solution is to use a [std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper) value to hold the reference to the origial `work_data`.

```cpp
void process_data( work_data& );
void do_work( work_data& data ) {
    be::task_pool pool;
    pool.submit(&process_data, std::ref(data) ); 
}
```
&nbsp;

It is also possible to pass values to functions using futures without modifying function signatures or adding additional overloads.

This is made possible because `be::task_pool` has the ability to match futures passed as arguments to `submit` against the value types of the task function. It can then placed the resulting task into a holding area while dependencies finish executing without blocking any additional threads. 

When the task arguments are ready the pool will execute the work load.


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

Task functions may also take a `be::stop_token` by value as their last argument to participate in the libraries support for cooperative cancellation. This type has a boolean conversion operator that will be true only if the pool has signalled abort.

Tasks may use this to break out of contiguous or other long running work allowing the pool to shutdown faster.

```cpp
be::task_pool pool;
auto do_until = []( auto timeout, be::stop_token abort ) { 
    while( !abort && std::chrono::now()<timeout ) {
        do_work();
    }
};
auto done = pool.submit( &do_until, std::chrono::now()+std::chrono::minutes(10) );
while ( done.wait(0s) != std::future_status::ready )
{
    if ( exit() ) {
        pool.abort();
        shutdown();
    }
    else {
        process_events();
    }
}
```
Above `be::task_pool::abort` will ask running tasks to cancel and if `do_until` did not take and check the `stop_token` the pool may need to wait the full 10 minutes it takes for the task to time out before being allowed to shutdown.

Note that the `stop_token` is not passed as an argument to `submit` as it can detect that `do_until` wants a token and will insert one for it when called.

Stop tokens may also be generated in user code by calling `be::task_pool::get_stop_token` which can be useful when combining multiple asynchronouse systems together.

```cpp
auto abort = pool.get_stop_token();
while (!abort)
{
    QApplication::instance()->processEvents();
}
```
&nbsp;

## Initialization and lifetime
  
Default constructed task_pool objects will hold the amount of threads return by [`std::thread::hardware_concurrency`](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency). This would typically be the amount of concurrent task a system can support in hardware.

To create a pool with a different amount of threads you may provide the desired amount of threads to the task_pool constructor.

```cpp
be::task_pool pool(1);
```

Task pool thread counts may be changed during the lifetime of the pool instance but not while the pool is executing tasks. To query the amount of threads currently used by a pool call `be::task_pool::get_thread_count` and to change the thread count call `be::task_pool::reset` with your desired amount of threads.

```cpp
be::task_pool pool(1);
// some time later
if ( pool.get_thread_count() < 8 ) {
    pool.reset(8);
}
```
`be::task_pool::reset` will block threads attempting to submit new tasks while waiting for current tasks to finish so should not be used during time sensitive program sections.

When a `task_pool` is destroyed it will ensure only that the currently executing tasks finish so its is up to the user to ensure destruction occurs only after desired workload is complete.

```cpp

struct work_item
{
    void operator()();
};

void do_work( std::vector<work_item> const& work) {
    be::task_pool pool(4);
    for( auto const& x : work )
    {
        pool.submit( x );
    }
    pool.wait_for_tasks();
}
```
When destroyed all enqueuing of work is stopped and any running threads are joined disgarding any tasks that have yet to be started. So if the wait statement at the end of `do_work` above is omitted only the work currently executing in the pools four threads would finish processing which is likely not the intended outcome.

Destruction of a `task_pool` also sets the `stop_token`.

```cpp
void check_data( work_data const&, be::stop_token );

void find_the_anwser( std::vector<work_data> const& data )
{
    be::task_pool pool;
    std::vector<std::future<void>> results(data.size());
    std::transform( data.begin(), data.end(), results.begin(), []( work_data const& x ) {
        return pool.submit( &check_data, std::ref(x) );
    });
    auto is_ready = []( std:future<void> const& x ) { 
        return x.wait(0s)==std::future_status::ready; 
    };
    while ( std::none_of( results.begin(), results.end(), is_ready ) )
    {
        std::this_thread::sleep_for(1ms);
    }
}
```
When any task of check_data completes we will fall through the while loop and the pool will be destroyed at the end of the function. Because the `check_data` takes a `be::stop_token` any running tasks of that function can stop doing work and return early allowing the pool destructor to complete and the `find_the_anwser` function to return to the caller. 
&nbsp;

## Using allocators

Tasks submitted to `be::task_pool` require storage on the heap until the task is invoked and this can become a limiting factor to applications. To help task_pool supports using custom allocators for storing tasks until executed as well as storing the shared state of the std::futures used to return results.

```cpp
struct LargeInputData
{
  // Lets picture some large dataset
};
LargeInputData generate_data();

struct ComplexResultData
{
    // Lets imagine some more large data here
};

ComplexResultData process_data( LargeInputData const& );
```
So given some API that operates on non trivial data we may find that we wish to control how the data is allocated in our program and that using some specific custom allocators would help performance in our application.

```cpp
cool_api::pool_allocator allocator;
be::task_pool pool;

auto result = pool.submit( std::allocator_arg_t{}, allocator, [work_data = generate_data()]() { 
    return process_data( work_data );
} );
```
Above we pass a custom allocator to submit which allow it to be used to allocate both the storage for the following task function that captures a potentially large object by value and also the storage needed for returning data asynchronously from `process_data` using a future. `std::allocator_arg_t` is and empty class used only to disambiguate the overloads to `be::task_pool::submit`

Now allocations are in control of `cool_api` that promises to be much faster then new/delete. 

[^1]: Futher improvents needed here to reduce copies and temporaries. Currently the most effcient way seems to be to take const reference in the task function and move/construct into the submit call. This will move into the bind expression and the function call will then reference out of this bind expresssion. Yes improvements are possible and will be done.

[^2]: Future-like objects must implement `get`, `wait`, `wait_for`, `wait_until` to be considered future-like
