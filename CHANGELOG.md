# v2.0
[X]: Remove allocator variants of task_pool::submit in favour of templating the task bool so that we do not need to pass allocators all the time.

[X]: Change external API wait_for_tasks to wait()

[ ]: Pools should be future-like

[X]: Dropping support for callables with templated call operators

[X]: Reduced overload set of `submit` and `make_deferred_task`

[x]: Allow pipeline tasks to take allocators and stop_tokens
