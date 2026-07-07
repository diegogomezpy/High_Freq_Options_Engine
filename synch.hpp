#pragma once

#include "protocol.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility> // Required to unlock std::move

namespace HFT::synchronization
{
    /**
     * @brief A header-only, thread-safe queue wrapper.
     * Implementing functions inside the class body automatically triggers
     * compiler inlining, removing function call overhead from the hot path.
     */
    class ConcurrentQueue
    {
    public:
        /**
         * @brief Construct the queue wrapper.
         */
        explicit ConcurrentQueue() = default;

        /**
         * @brief Destroy the queue wrapper.
         */
        ~ConcurrentQueue() = default;

        // Prevent copying and moving to preserve strict address integrity
        // for internal synchronization variables.
        ConcurrentQueue(const ConcurrentQueue &) = delete;
        ConcurrentQueue &operator=(const ConcurrentQueue &) = delete;
        ConcurrentQueue(ConcurrentQueue &&) = delete;
        ConcurrentQueue &operator=(ConcurrentQueue &&) = delete;

        /**
         * @brief Copies an incoming market tick packet into the internal container.
         * Automatically inlined.
         */
        void push(const protocol::MarketPacket &packet) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(packet);
            cv_.notify_one(); // Wake up the sleeping strategy thread
        }

        /**
         * @brief Moves a temporary market tick packet into the internal container.
         * Automatically inlined.
         */
        void push(protocol::MarketPacket &&packet) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(packet)); // Zero-copy ownership transfer
            cv_.notify_one();
        }

        /**
         * @brief Blocking pop operation. Puts the strategy thread to sleep
         * safely if no market data is available.
         */
        void pop(protocol::MarketPacket &out_packet) noexcept
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Sleep if empty; automatically handles spurious false-alarm wakeups
            cv_.wait(lock, [this]
                     { return !queue_.empty(); });

            out_packet = std::move(queue_.front());
            queue_.pop();
        }

        /**
         * @brief Non-blocking check. Instantly returns false if the room is locked
         * or if the plate is empty, protecting the hot path from stalling.
         */
        [[nodiscard]] bool try_pop(protocol::MarketPacket &out_packet) noexcept
        {
            // Try to grab the key. If someone else has it, give up immediately!
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);

            if (!lock.owns_lock() || queue_.empty())
            {
                return false;
            }

            out_packet = std::move(queue_.front());
            queue_.pop();
            return true;
        }

    private:
        std::queue<protocol::MarketPacket> queue_; ///< Sequential buffer
        std::mutex mutex_;                         ///< Room key
        std::condition_variable cv_;               ///< Notification buzzer
    };
} // namespace HFT::synchronization