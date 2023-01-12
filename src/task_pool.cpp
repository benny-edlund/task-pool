#include "task_pool.h"
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace be {

struct task_pool::Impl
{
    mutable std::mutex               tasks_mutex_       = {};
    std::queue< task_proxy >         tasks_             = {};
    mutable std::mutex               check_tasks_mutex_ = {};
    std::vector< task_proxy >        tasks_to_check_    = {};
    std::condition_variable          task_added_        = {};
    std::atomic< bool >              waiting_{ false };
    std::atomic< bool >              paused_{ false };
    std::atomic< bool >              abort_{ false };
    std::condition_variable          task_completed_ = {};
    std::atomic< std::size_t >       tasks_queued_{ 0 };
    unsigned                         thread_count_ = 0;
    std::unique_ptr< std::thread[] > threads_; //  NOLINT (c-arrays)

    explicit Impl( unsigned thread_count )
        : thread_count_( determine_thread_count( thread_count ) )
        , threads_( std::make_unique< std::thread[] >( thread_count ) ) // NOLINT (c-arrays)
    {
        create_threads();
    }
    ~Impl() { destroy_threads(); }

    Impl( Impl const& )            = delete;
    Impl( Impl&& )                 = delete;
    Impl& operator=( Impl const& ) = delete;
    Impl& operator=( Impl&& )      = delete;

    void create_threads()
    {
        abort_   = false;
        threads_ = std::make_unique< std::thread[] >( thread_count_ ); // NOLINT
        for ( unsigned i = 0; i < thread_count_; ++i )
        {
            threads_[i] = std::thread( &Impl::worker, this );
        }
    }

    void destroy_threads()
    {
        {
            std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
            abort_ = true;
            task_added_.notify_all();
        }
        for ( unsigned i = 0; i < thread_count_; ++i )
        {
            if ( threads_[i].joinable() )
            {
                threads_[i].join();
            }
        }
        threads_.reset();
        thread_count_ = 0;
    }

    static unsigned determine_thread_count( const unsigned thread_count )
    {
        // we need at least two threads to process work and check futures
        if ( thread_count > 0 )
        {
            return thread_count;
        }
        else
        {
            if ( std::thread::hardware_concurrency() > 0 )
            {
                return std::thread::hardware_concurrency();
            }
            else
            {
                return 1;
            }
        }
    }

    void add_task( task_proxy&& proxy )
    {
        if ( proxy.check_task( proxy.storage.get() ) )
        {
            {
                std::unique_lock< std::mutex > lock( tasks_mutex_ );
                tasks_.push( std::move( proxy ) );
            }
            ++tasks_queued_;
        }
        else
        {
            std::unique_lock< std::mutex > lock( check_tasks_mutex_ );
            tasks_to_check_.push_back( std::move( proxy ) );
        }
        task_added_.notify_one();
    }

    std::size_t get_tasks_queued() const { return tasks_queued_; }

    std::size_t get_tasks_running() const
    {
        std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
        return tasks_queued_ - tasks_.size();
    }

    std::size_t get_tasks_total() const
    {
        std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
        return tasks_.size();
    }

    unsigned get_thread_count() const { return thread_count_; }

    bool is_paused() const { return paused_; }

    void pause() { paused_ = true; }

    void reset( const unsigned thread_count )
    {
        const bool was_paused = paused_;
        paused_               = true;
        wait_for_tasks();
        destroy_threads();
        thread_count_ = determine_thread_count( thread_count );
        paused_       = was_paused;
        create_threads();
    }

    void unpause() { paused_ = false; }

    void wait_for_tasks()
    {
        waiting_ = true;
        std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
        task_added_.notify_all();
        task_completed_.wait(
            tasks_lock, [this] { return ( tasks_queued_ == ( paused_ ? tasks_.size() : 0 ) ); } );
        waiting_ = false;
    }

    void cooperative_abort() { destroy_threads(); }

    std::atomic< bool > const& get_stop_token() const { return abort_; }

    std::vector< task_proxy > checker() // must run with waiting_task_mutex held
    {
        if ( abort_ )
        {
            return {};
        }
        std::vector< task_proxy > ready_tasks;
        {
            auto const start   = std::begin( tasks_to_check_ );
            auto const stop    = std::end( tasks_to_check_ );
            auto const removed = std::remove_if( start, stop, []( auto const& proxy ) {
                return proxy.check_task( proxy.storage.get() );
            } );
            if ( removed != stop )
            {
                ready_tasks.reserve(
                    static_cast< std::size_t >( std::distance( removed, tasks_to_check_.end() ) ) );
                std::copy( std::make_move_iterator( removed ),
                           std::make_move_iterator( stop ),
                           std::back_inserter( ready_tasks ) );
            }
            tasks_to_check_.erase( removed, stop );
        }
        return ready_tasks;
    }

    /**
     * @brief Worker thread task
     *
     * @details All threads run the worker function. Tasks are allowed to take futures as arguments
     * and instead of blocking an entire thread checking futures, something that can quickly fill up
     * a thread pool we absorb the future checking into a argument wrapper for the task function.
     * Then in the worker function we utilize unused threads to check the status of these futures
     * and when tasks are ready to execute we schedule them.
     *
     * The idea would be that there is always some thread waiting on the tasks_mutex_ so before we
     * take it we try to take the checker mutex and if we succeed we check all tasks that depend on
     * futures. Once we have checked the futures we wake up any waiting thread to be the next
     * checker before we take the tasks mutex.
     */
    void worker()
    {
        for ( ;; )
        {
            {
                std::vector< task_proxy > ready_tasks;
                // workers first try to become the checker
                std::unique_lock< std::mutex > checks_lock( check_tasks_mutex_, std::try_to_lock );
                if ( checks_lock.owns_lock() && !tasks_to_check_.empty() )
                {
                    ready_tasks = checker();
                    task_added_.notify_one(); // wake up the next checker
                }
                for ( auto& proxy : ready_tasks )
                {
                    add_task( std::move( proxy ) );
                }
            }
            std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
            if ( abort_ )
            {
                break;
            };
            task_added_.wait( tasks_lock, [this] { return !tasks_.empty() || abort_; } );
            if ( paused_ || tasks_.empty() )
            {
                // we where woken for abort or to be the next checker
                continue;
            }
            if ( abort_ )
            {
                return;
            }
            task_proxy proxy = std::move( tasks_.front() );
            tasks_.pop();
            tasks_lock.unlock();
            proxy.execute_task( proxy.storage.get() );
            --tasks_queued_;
            if ( waiting_ )
            {
                task_completed_.notify_one();
            }
        }
    }
};

task_pool::task_pool( const unsigned thread_count )
    : impl_{ new Impl( thread_count ) }
{
}

task_pool::~task_pool()
{
    impl_.reset();
    // if (impl_ != nullptr) { impl_->cooperative_abort(); }
}

task_pool::task_pool( task_pool&& other ) noexcept
    : impl_( std::move( other.impl_ ) )
{
    other.impl_.reset();
}

task_pool& task_pool::operator=( task_pool&& other ) noexcept
{
    wait_for_tasks();
    impl_.reset();
    std::swap( other.impl_, impl_ );
    return *this;
}

std::size_t task_pool::get_tasks_queued() const
{
    return impl_->get_tasks_queued();
}

std::size_t task_pool::get_tasks_running() const
{
    return impl_->get_tasks_running();
}

std::size_t task_pool::get_tasks_total() const
{
    return impl_->get_tasks_total();
}

unsigned task_pool::get_thread_count() const
{
    return impl_->get_thread_count();
}

bool task_pool::is_paused() const
{
    return impl_->is_paused();
}

void task_pool::pause()
{
    impl_->pause();
}

void task_pool::reset( const unsigned thread_count_ )
{
    impl_->reset( thread_count_ );
}

void task_pool::unpause()
{
    impl_->unpause();
}

void task_pool::wait_for_tasks()
{
    impl_->wait_for_tasks();
}

stop_token task_pool::get_stop_token() const
{
    return stop_token{ impl_->get_stop_token() };
}

void task_pool::push_task( task_proxy&& task )
{
    impl_->add_task( std::move( task ) );
}

task_pool::task_proxy::task_proxy( task_proxy&& other ) noexcept
    : check_task( other.check_task )
    , execute_task( other.execute_task )
    , storage( std::move( other.storage ) )
{
    other.execute_task = []( void* /*unused*/ ) {};
}

void task_pool::abort()
{
    impl_->cooperative_abort();
}

task_pool::task_proxy& task_pool::task_proxy::operator=( task_proxy&& other ) noexcept
{
    check_task         = other.check_task;
    execute_task       = other.execute_task;
    storage            = std::move( other.storage );
    other.execute_task = []( void* /*unused*/ ) {};
    return *this;
}

} // namespace be
