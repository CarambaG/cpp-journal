#pragma once

#include "input_parser.hpp"
#include "journal/logger.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <iosfwd>
#include <mutex>
#include <thread>

namespace journal_app {

// Owns the background writer required by part 2 of the assignment.
class JournalApplication final {
public:
    JournalApplication(journal::ILogger& logger, std::ostream& errorOutput);
    ~JournalApplication();

    JournalApplication(const JournalApplication&) = delete;
    JournalApplication& operator=(const JournalApplication&) = delete;
    JournalApplication(JournalApplication&&) = delete;
    JournalApplication& operator=(JournalApplication&&) = delete;

    // Thread-safely transfers a message to the writer queue.
    [[nodiscard]] bool submit(Message message);

    // Stops accepting new messages, writes all queued messages and joins the
    // worker thread. Calling stop more than once is allowed.
    void stop() noexcept;

    [[nodiscard]] bool hasWriteErrors() const noexcept;

private:
    void writerLoop() noexcept;

    journal::ILogger& logger_;
    std::ostream& errorOutput_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::deque<Message> queue_;
    bool stopping_{false};
    std::thread writerThread_;
    std::atomic<bool> writeError_{false};
};

}  // namespace journal_app
