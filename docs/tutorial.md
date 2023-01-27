# Tutorial
`task_pool` strives to be unsurpricing in its use and as such much of what is covered here will be likely sound very familiar.

The features of this library are certainly not unique and similar implementations can be found in many other libraries as well. What is a bit unique in this library however is that the feature we will cover here are found in a single library that works down to C++14 without relying on any external libraries.

&nbsp;
## Basic use
```cpp
be::task_pool pool;
auto task = []() { std::cerr<< "Hello World!\n"; };
pool.submit( task );
pool.wait_for_tasks();
```
Here we first default construct a pool object. A lambda is then declared and passed by reference to `submit` as a task to execute in the pool. 

Finally we use `wait_for_tasks` to ensure all tasks submitted to the pool are completed before continuing.

&nbsp;

## Task types
Most callable value types can be used as a task. Free and member functions pointers can be passed by value. Callable objects such as lambdas and std::function as well as user defined types implementing an appropriate call operator can be passed both by value and by reference as long as relevant life time rules are observed.


&nbsp;
```cpp
struct work_data; // lets imagine some large data structure here
work_data make_data();  
bool process_data( work_data );
```
Given some some external API to create and process a dataset the most obvious way to off load its work from the main thread using `task_pool` can be to put it all in a single lambda.
```cpp
auto result = pool.submit( []{ return process_data( make_data() ); } );
```
The lambda is passed by value and stored in the `task_pool` executing the two functions in sequence. A future is returned to acquire the result when the process is completed.

The main drawback with this approach is that both function calls must complete or fail for our program to finish. If something unexpected happens that requires our program to exit we have no way to stop the task early.

A better way may be to pass the functions pointers themselves as tasks to execute.

```cpp
auto future = pool.submit( &make_data );
auto result = pool.submit( &process_data, std::move(future) );
```
Here we pass the function pointers by value directly to the thread pool capturing a future value from `make_data` and utilizing the `task_pool` to efficiently wait for this data to be available before processing it in the call to `process_data`.

Our program now has a chance to exit before calling the second task and this can be futher improved by as we will see later on but first...
&nbsp;

### More callable types


```cpp
struct data;

struct processor
{   
    using ptr = std::unique_ptr<processor>;
    virtual void run(data&) const = 0;
};

void process_frame( data& frame) {
    std::vector<processor::ptr> workload = get_workload();
    for( processor::ptr const& work : workload )
    {
        pool.submit(&processor::run, work.get(), std::ref(frame) );
    }
}
```
Here we have a user defined type and we pass its `run` function pointer by value along with pointers to the instances we wish to execute the method on. Additionally a reference to some data structure is passed to the processors. With member functions as well as when passing data by references we need to make sure we are careful to observe the lifetime requirements of our dataset.

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

Here `task_pool` will check that `Work::operator()( Data )` is a valid function call prior to submitting and will generate compilation error in the case it is not.

&nbsp;
## Return values

As we have seen any task submitted to a pool returns a [`std::future`](https://en.cppreference.com/w/cpp/thread/future) that can be used to get the result value when its available as the task function returns.

```cpp
be::task_pool pool;
auto task = []() { return 42; };
auto result = pool.submit( task );
auto the_awnser = result.get();
```

When using `task_pool` it is only required to capture the futures of tasks that return values however using `std::future<void>`from functions that do not return values to check for task completions using its  [`std::future::wait_for`](https://en.cppreference.com/w/cpp/thread/future/wait_for) API is still recommended as it is typically well optimized by compilers.

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

Its possible to pass arguments to tasks when submitting them to the pool. Required arguments for functions are simply passed to `submit` after the function itself. This is for example how we pass the instance pointer to for member functions.

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

The solution may be to use a [std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper) value to hold the reference to the origial `work_data`.

```cpp
void process_data( work_data& );
void do_work( work_data& data ) {
    be::task_pool pool;
    pool.submit(&process_data, std::ref(data) ); 
}
```

### Futures
It is also possible to pass values to functions using futures without modifying function signatures or adding additional overloads.

This is made possible because `be::task_pool` has the ability to match futures passed as arguments to `submit` against the value types of the task function. It can then place the resulting task into a holding area while dependencies finish executing without blocking any additional threads. 

When the task arguments are ready the pool will queue the task into the pool where it will be executed.


```cpp
namespace api 
{
    struct LargeData;
    std::future<LargeData> generate_data_async();
    int process_data( LargeData );
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

## Function composition
By including `task_pool/pipes.h` applications may use the pipe operator to define pipelines out of descreet functions running in a task_pool. 

This can help readability of your program a lot as it evolves.

Let's imagine again some external api that operates on a special datatype we are interested in.
```cpp
namespace api {
    struct LargeData;
    std::vector<LargeData> make_data();         // creates some special data
    int process_data( std::vector<LargeData> ); // returns error code or zero
}
```

A program utilizing pipes to dispatch the data generation and processing as a sequence can be quite minimal and easy to read.
```cpp
#include <task_pool/task_pool.h>
#include <task_pool/pipes.h>
#include <largedata.h>

int main()
{
    be::task_pool pool;
    auto pipe = pool | api::make_data | api::process_data
    return pipe.get();
}
```

In comparison the standard api used in a program gets a bit more verbose.

```cpp
#include <task_pool/task_pool.h>
#include <task_pool/pipes.h>
#include <largedata.h>

int main()
{
    be::task_pool pool;
    auto data = pool.submit(&api::make_data);
    auto result = pool.submit(&api::process_data, std::move(data) );
    return result.get();
}
```
It's a bit more involed to read the code as there is more language noise and also we must exercise some braincells to evaluate the flow of the `data` variable as we need to verify that it is not used after being moved. 

Still not too bad. 

Now lets say the next developer working on the project needs to insert logging prior to processing. People can never get enough logging it seems...


```cpp
#include <task_pool/task_pool.h>
#include <task_pool/pipes.h>
#include <largedata.h>

template<typename Container>
void print_container( Container const& ) { /* we know what goes here right? */}

std::vector<api::LargeData> log_data( std::vector<api::LargeData> data ) {
    print_container( data );
    return data;
}
```
First with the standard api
```cpp
int main()
{
    be::task_pool pool;
    auto data = pool.submit(&api::make_data);
    auto data_logged = pool.submit(&log_data, std::move(data) );
    auto result = pool.submit(&api::process_data, std::move(data_logged) );
    return result.get();
}
```
Now using pipes
```cpp
int main()
{
    be::task_pool pool;
    auto pipe = pool | api::make_data | log_data | api::process_data;
    return pipe.get();
}

```
The code using the standard api quickly becomes a lot more involved to read and verify as more lines need to change and more data dependencies need to be understood. The version using pipes in contrast features a single addition in the pipeline that is neatly diffirentiated from its previous iteration.

As `be::task_pool`s pipe implementation utilizes the lazy task arguments we read about previously to pass values between pipeline stages it also quite naturally falls into value oriented programing which is a much safer way to structure asynchronous programs. Functions used in such a pipeline can simply take and return inputs by value and `be::task_pool` will convert these into futures passed lazily between stages using `submit` by wrapping each stage into a dynamically generated type that suits the function, the `Pipe`. 

This type contains only a reference to the executing pool and the `Future` of the previous stage. When a new stage is added this future is moved into the task_pool as a lazy argument and as such the resulting temporaries require very little storage. There is only ever one future on the loose that can control the conclusion of the entire pipeline.

Pipe object that hold valid futures will call `Future::wait()` on destruction which means that uncaptured pipelines will safely block as if they where direct function calls.
```cpp
int main()
{
    be::task_pool pool;
    pool | api::make_data | log_data | api::process_data;
    return 0;
}
```
The program above will safely execute the entire pipeline before returning. As the last binary operator concludes it leaves a temporary Pipe object that owns a valid future and since the destrcution of this temporary must occur prior to executing the next line we are guarenteed no dangling work is left over when returning.

`Pipe` objects that are captured are also `future-like` objects and can as such be used as lazy arguments to other tasks.

```cpp
int main()
{
    be::task_pool pool;
    {
        auto pipe_one = pool | api::make_data | log_data | api::process_data;
        auto pipe_two = pool | api::make_data | log_data | api::process_data;

        pool.submit( []( int x, int y ) {
            fmt::print("Data processing complete! First result: {} Second result: {}\n", x, y);
        }, std::move(pipe_one),std::move(pipe_two) );
    }
    pool.wait_for_tasks();
    return 0;
}
```
Above we use `task_pool`s type erased task storage to safely move the pipelines out of their stack scope to continue doing other work after leaving the scope where they where defined.

## Custom promise types

As afore mentioned it is possible to use any future-like object as a lazy argument provided they support the same api as std::future.
```cpp
template<typename T>
struct Future
{
    enum class Status
    {
        ready,
        timeout,
        deferred
    };
    T   get();
    void   wait();
    Status wait_for( std::chrono::steady_clock::duration );
    Status wait_until( std::chrono::steady_clock::time_point );
};
```
Above is some custom future type defined in an api. This library requires the methods `get()`, `wait()`  to be called without arguments but it does not check the return types. For the methods `wait_for` and `wait_until` the library requires the that the methods are called with some appropriate `std::chrono` type matching std::future and it requires that the methods returns some kind of enum type that have the definitions `ready`, `timeout` and `deferred`. Any object furfiling this contract may be used as a lazy argument to a task function.

It is also possible to customize what promise type should be used in the task execution which ultimately also affects what future type is returned from `be::task_pool::submit`

```cpp
template< typename T >
struct Promise
{
    Promise();
    Future get_future();
    void set_value( T );
    void set_exception( std::exception_ptr );
};

```
Above a custom promise type is defined that follows the api of std::promise. It must be a template taking a single template parameters and it must have the methods `get_future`, `set_value` and `set_exception`

The promise type can be used in the pool by specifying it as a template parameter to `submit`

```cpp
std::vector<int> make_data();
bool process_data( std::vector<int> );

be::task_pool pool;
auto data = pool.submit<Promise>(&make_data);
auto result = pool.submit<Promise>(&process_data, std::move(data));
```
Above the custom Promise template is used to instantate the task promise type. `submit` will return the future type returned by this promise.

Each task may have a different promise that suits the purpose.

For tasks that will take allocators the custom promise additionally needs to be allocator constructible by defining a templated constructor like below.

```cpp
template<typename T>
struct Promise
{
    Promise();
    template< template<typename> class Allocator, typename T >
    Promise( std::allocator_arg_t, Allocator<T> const& );
    Future get_future();
    void set_value( T );
    void set_exception( std::exception_ptr );
};
```

This will then be allowed to be used in calls to `submit` that utilize allocators.

```cpp
std::vector<int, FastAlloc<int> > make_data(std::allocator_arg_t, FastAlloc<int> const& alloc);
bool process_data( std::vector<int, FastAlloc<int>> );

be::task_pool pool;
std::allocator_arg_t tag;
FastAlloc<int> alloc;
auto data = pool.submit<Promise>(tag,alloc,&make_data);
auto result = pool.submit<Promise>(&process_data, std::move(data));
```
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

When a `task_pool` is destroyed it will ensure only that the currently executing tasks finish so its is up to the user to ensure destruction occurs only after desired workload is completed.

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
When any task of check_data completes we will fall through the while loop and the pool will be destroyed at the end of the function. Because `check_data` takes a `be::stop_token` any running tasks of that function has the ability stop doing work and return early allowing the pool destructor to complete and the `find_the_anwser` function to return to the caller. 
&nbsp;

## Using allocators

Tasks submitted to `be::task_pool` require storage on the heap until the task is invoked and this can become a limiting factor to applications. 

To help task_pool supports using custom allocators.

```cpp

namespace beans {
    struct bean;
    template<typename T = bean>
    struct pool_allocator;
}

```
First lets assume the `beans` library defines some allocator type that we would like to use. We can use this allocator in our pool to allocate storage for our task wrappers and any storage the given promise types requires by simple taking it as a template parameter when declaring our pool.

```cpp

beans::pool_allocator<> alloc(1'000'000);
be::task_pool_t<beans::pool_allocator> pool(alloc);

```

Here we initialize our (fictional) allocator with a million beans and then declare that our pool will use this allocator by passing it as a template parameter to `be::task_pool_t` following by constructing our pool taking a reference to the allocator instance. This would likely only be necessary if the allocator is stateful or if it can not be default constructible. 

The default allocator for `be::task_pool_t` is perhaps unsupricingly [`std::allocator`](https://en.cppreference.com/w/cpp/memory/allocator) and it for example can be default constructed and hence does not need to be passed to the constructor. In fact `be::task_pool` that we have been using so far is just a type alias for `be::task_pool_t<std::allocator>`

```cpp

struct LargeData; // lets picture some large data there

using Vector =  std::vector<LargeData, beans::pool_allocator<LargeData>>;
auto make_data = [](std::allocator_arg_t, Allocator const& alloc, std::size_t x) {
    return Vector( x, alloc );
}

auto process_data = []( Vector x ) {
    for( auto& element : x )
    {
        change_item( element );
    }
    return x;
}

auto data = pool.submit( make_data, 1'000'000 ); 
auto result = pool.submit( process_data, std::move(data)); 

```

Here the first task function `make_data` adds the special signature `( std::allocator_arg_t, beans::pool_allocator<LargeData> const&, ... )` which allows `task_pool::submit` to detect that it wants the pool allocator to be passed down when it is called. The allocated vector is then passed on by value to the process function. This is made efficient as future input arguments to functions utilize RVO from `std::future::get` to pass resulting value directly to the function call.

`std::allocator_arg_t` is an empty class used only to detect the need to pass an allocator to the task.

&nbsp;


[^1]: Futher improvents needed here to reduce copies and temporaries. Currently the most effcient way seems to be to take const reference in the task function and move/construct into the submit call. This will move into the bind expression and the function call will then reference out of this bind expresssion. Yes improvements are possible and will be done.

[^2]: Future-like objects must implement `get`, `wait`, `wait_for`, `wait_until` to be considered future-like
