# v3.0
Refined api after trials with various of allocator implementations.

* Require fully specified allocator type for pool definition.
  * Allowing the pool to be defined using only the allocator template was convinient however ultimately unhelpful when rebinding allocators that require many template arguments. This required a interface change to the pool class and hence bumps the major version but there are no interface changes other then this so v2.0 code will be source compatible.
* Corrected detection of member function pointers used as task with lazy arguments.
  * This was a blind spot in the test suite
* Added some example applications

# v2.0
Refined version reducing overload set by binding allocator to the pool itself and adding pipe operator and future api for function composition

* Remove allocator variants of task_pool::submit in favour of templating the task bool so that we do not need to pass allocators all the time.
* Change external API wait_for_tasks to wait()
* Pools should be future-like
* Dropping support for callables with templated call operators
* Reduced overload set of `submit` and `make_deferred_task`
* Allow pipeline tasks to take allocators and stop_tokens
* Changed `task_pool.h` to simply `pool.h`
* API is now const and noexcept correct ( to the best of my mortal abilities )
# v1.0
Initial version supporting full set of features

* Allow tasks from function pointers, free and member
* Allow cancelling tasks after started by passing token type
* Allow using custom allocators to allocate future states
* Allow using custom allocators to allocate tasks storage
* Allow using custom allocators inside tasks perform allocations