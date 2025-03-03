#include <mutex>
#include <condition_variable>

class SimpleBarrier {
public:
    explicit SimpleBarrier(unsigned int count) : m_count(count), m_generation(0) {}

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        unsigned int gen = m_generation;

        // Decrement the remaining arrivals
        if (--m_count == 0) {
            // All threads reached the barrier
            m_generation++;
            m_cond.notify_all();
        } else {
            // Wait until the barrier is released
            m_cond.wait(lock, [this, gen] { return gen != m_generation; });
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    unsigned int m_count;
    unsigned int m_generation;
};
