# v2.0
Refined version reducing overload set by binding allocator to the pool itself and adding pipe operator for function composition

- [x] Remove allocator variants of task_pool::submit in favour of templating the task bool so that we do not need to pass allocators all the time.
- [x] Change external API wait_for_tasks to wait()
- [x] Pools should be future-like
- [x] Dropping support for callables with templated call operators
- [x] Reduced overload set of `submit` and `make_deferred_task`
- [x] Allow pipeline tasks to take allocators and stop_tokens
- [x] Changed `task_pool.h` to simply `pool.h`
- [x] API is now const and noexcept correct ( to the best of my mortal abilities )
# v1.0
Initial version supporting full set of features

- [x] Allow tasks from function pointers, free and member
- [x] Allow cancelling tasks after started by passing token type
- [x] Allow using custom allocators to allocate future states
- [x] Allow using custom allocators to allocate tasks storage
- [x] Allow using custom allocators inside tasks perform allocations