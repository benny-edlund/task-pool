#include <catch2/catch.hpp>
#include <task_pool.h>


TEST_CASE("task_pool construction", "[task_pool]")
{
    be::task_pool pool(2);
    REQUIRE(pool.get_num_threads(), 5)
}
