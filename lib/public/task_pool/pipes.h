#pragma once
#include <chrono>
#include <task_pool/fallbacks.h>
#include <task_pool/task_pool.h>
#include <task_pool/traits.h>
#include <type_traits>
#include <utility>

namespace be {

template< typename Func, typename... Args >
auto make_pipe( be::task_pool& pool, Func&& func, Args&&... args )
{
    using Future    = decltype( std::declval< be::task_pool >().submit(
        std::declval< Func >(), std::forward< Args >( std::declval< Args >() )... ) );
    using ValueType = decltype( std::declval< Future >().get() );
    using StatusType =
        decltype( std::declval< Future >().wait_for( std::declval< std::chrono::seconds >() ) );
    struct pipe_
    {
        be::task_pool& pool_;
        Future         future_;
        pipe_( be::task_pool& x, Future&& y )
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

        void                    wait() { future_.wait(); }
        ValueType               get() { return future_.get(); }
        BE_NODISGARD StatusType wait_for( std::chrono::steady_clock::duration ns )
        {
            return future_.wait_for( ns );
        }
        BE_NODISGARD StatusType wait_until( std::chrono::steady_clock::time_point ns )
        {
            return future_.wait_until( ns );
        }
    };
    return pipe_( pool,
                  pool.submit( std::forward< Func >( func ), std::forward< Args >( args )... ) );
}

template< typename Func >
auto operator|( be::task_pool& pool, Func&& f )
{
    return make_pipe( pool, std::forward< Func >( f ) );
}

template< typename Pipe, typename Func, std::enable_if_t< is_pipe< Pipe >::value, bool > = true >
auto operator|( Pipe&& p, Func&& f )
{
    return make_pipe( p.pool_, std::forward< Func >( f ), std::move( p.future_ ) );
}

} // namespace be