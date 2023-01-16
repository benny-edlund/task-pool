#include <task_pool/task_pool.h>
#include <atomic>
#include <cassert>

int main() {
    be::task_pool pool;
    std::atomic_int called{ 1 };    
    auto future = pool.submit([](std::atomic_int& x ) { --x; }, std::ref(called) );
    future.wait();
    return called.load(); // zero success
}
