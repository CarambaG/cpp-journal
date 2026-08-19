#include "journal_application.hpp"

#include <iostream>
#include <utility>

namespace journal_app {

JournalApplication::JournalApplication(journal::ILogger& logger,
                                       std::ostream& errorOutput)
    : logger_(logger),
      errorOutput_(errorOutput),
      writerThread_(&JournalApplication::writerLoop, this) {}

JournalApplication::~JournalApplication() {
    stop();
}

bool JournalApplication::submit(Message message) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (stopping_) {
            return false;
        }
        queue_.push_back(std::move(message));
    }
    queueCondition_.notify_one();
    return true;
}

void JournalApplication::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
    }
    queueCondition_.notify_all();

    if (writerThread_.joinable()) {
        writerThread_.join();
    }
}

bool JournalApplication::hasWriteErrors() const noexcept {
    return writeError_.load(std::memory_order_relaxed);
}

void JournalApplication::writerLoop() noexcept {
    for (;;) {
        Message message;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock,
                                 [this] { return stopping_ || !queue_.empty(); });

            if (queue_.empty()) {
                if (stopping_) {
                    return;
                }
                continue;
            }

            message = std::move(queue_.front());
            queue_.pop_front();
        }

        const journal::WriteResult result = message.level.has_value()
                                                ? logger_.log(message.text,
                                                              *message.level)
                                                : logger_.log(message.text);

        if (result == journal::WriteResult::Written ||
            result == journal::WriteResult::Filtered) {
            continue;
        }

        writeError_.store(true, std::memory_order_relaxed);
        errorOutput_ << "Cannot write journal message: "
                     << journal::toString(result) << '\n';
    }
}

}  // namespace journal_app
