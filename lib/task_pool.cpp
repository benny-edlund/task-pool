#include <task_pool/pool.h>
namespace be {

TASKPOOL_API stop_token::operator bool()
{
    return token.load();
}

template class task_pool_t< std::allocator< void > >;
} // namespace be
