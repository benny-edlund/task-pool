#pragma once
#include <chrono>
#include <task_pool/fallbacks.h>
#include <task_pool/pool.h>
#include <task_pool/traits.h>
#include <type_traits>
#include <utility>

namespace be {

struct detach_t
{
    template< typename Pipe, std::enable_if_t< is_pipe< Pipe >::value, bool > = true >
    auto consume_future( Pipe& pipe ) const noexcept
    {
        return static_cast< typename Pipe::future_type >( pipe );
    }
};
static detach_t detach{}; // NOLINT

template< typename Allocator, typename Func, typename... Args >
TASKPOOL_HIDDEN auto make_pipe( be::task_pool_t< Allocator >& pool, Func&& func, Args&&... args )
{

    //
    // Regarding Wno-unused-local-typedefs on APPLE. For some reason these following typesdefs are
    // considered unused by apple-clang although they are most certainly used in the defined class
    // 
    struct TASKPOOL_HIDDEN pipe_
    {
        using future_type    = decltype( std::declval< be::task_pool_t< Allocator > >().submit(
            std::declval< Func >(), std::forward< Args >( std::declval< Args >() )... ) );
        using value_type     = decltype( std::declval< future_type >().get() );
        using status_type    = decltype( std::declval< future_type >().wait_for(
            std::declval< std::chrono::seconds >() ) );
        using allocator_type = Allocator;

        be::task_pool_t< allocator_type >& pool_;
        future_type                        future_;
        pipe_( be::task_pool_t< allocator_type >& x, future_type&& y )
            : pool_( x )
            , future_( std::move( y ) )
        {
        }
        ~pipe_()
        {
            if ( future_.valid() )
            {
                future_.wait();
            }
        }
        pipe_( pipe_ const& )            = delete;
        pipe_& operator=( pipe_ const& ) = delete;
        pipe_( pipe_&& x ) noexcept
            : pool_( x.pool_ )
            , future_( std::move( x.future_ ) ){};
        pipe_& operator=( pipe_&& x ) noexcept = delete;

        explicit operator future_type() noexcept { return std::move( future_ ); }

        void                     wait() const { future_.wait(); }
        value_type               get() { return future_.get(); }
        BE_NODISGARD status_type wait_for( std::chrono::steady_clock::duration ns ) const
        {
            return future_.wait_for( ns );
        }
        BE_NODISGARD status_type wait_until( std::chrono::steady_clock::time_point ns ) const
        {
            return future_.wait_until( ns );
        }
    };
    return pipe_( pool,
                  pool.submit( std::forward< Func >( func ), std::forward< Args >( args )... ) );
}

template< typename TaskPool,
          typename Func,
          std::enable_if_t< is_pool< TaskPool >::value &&
                                !std::is_same< std::decay_t< Func >, be::detach_t >::value,
                            bool > = true >
auto operator|( TaskPool& pool, Func&& f )
{
    return make_pipe( pool, std::forward< Func >( f ) );
}

template< typename Pipe,
          typename Func,
          std::enable_if_t< is_pipe< Pipe >::value &&
                                !std::is_same< std::decay_t< Func >, be::detach_t >::value,
                            bool > = true >
auto operator|( Pipe&& p, Func&& f )
{
    return make_pipe( p.pool_, std::forward< Func >( f ), std::move( p.future_ ) );
}

template< typename Pipe, std::enable_if_t< is_pipe< Pipe >::value, bool > = true >
void operator|( Pipe&& p, detach_t const& x )
{
    x.consume_future( p );
}

} // namespace be