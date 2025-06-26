#include "esphome/components/line_server/ring_buffer.h"

#include "esphome/core/hal.h"

namespace esphome {
  namespace line_server {

    RingBuffer::RingBuffer(size_t size, const std::string &terminator)
        : size_(size), buf_(new uint8_t[size]), terminator_(terminator) {}

    bool RingBuffer::write(uint8_t byte) {
      if (free_space() == 0)
        return false;
      buf_[index_(head_++)] = byte;
      last_write_time_ = ::esphome::millis();
      return true;
    }

    size_t RingBuffer::write_array(const uint8_t *data, size_t len) {
      size_t written = 0;
      for (size_t i = 0; i < len; i++) {
        if (!write(data[i]))
          break;
        written++;
      }
      return written;
    }

    std::string RingBuffer::read_line() {
        size_t pos = tail_;

        while (pos != head_) {
            // Try to match terminator starting from pos
            bool match = true;
            for (size_t i = 0; i < terminator_.size(); ++i) {
                if ((pos + i) == head_ || buf_[index_(pos + i)] != terminator_[i]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                size_t line_len = (pos + terminator_.size()) - tail_;
                std::string line(line_len, '\0');
                for (size_t i = 0; i < line_len; ++i)
                    line[i] = static_cast<char>(buf_[index_(tail_ + i)]);

                tail_ += line_len;
                return line;
            }

            pos++;
        }

        return "";
    }

    std::string RingBuffer::read_partial() {
        std::string result;
        result.reserve(available());
        size_t pos = tail_;
        while (pos != head_) {
            result.push_back(static_cast<char>(buf_[index_(pos)]));
            pos = (pos + 1) % size_;
        }
        return result;
    }

    std::string RingBuffer::flush_if_idle(uint32_t now, uint32_t timeout_ms) {
        if ((now - last_write_time_) < timeout_ms || available() == 0)
            return "";

        std::string partial = read_partial();
        tail_ = head_;  // clear after read
        return partial;
    }

    RingBuffer::BufferSlice RingBuffer::next_write_chunk() {
        if (head_ >= tail_) {
            // Case 1: Normal case, head is ahead of tail
            size_t space = (tail_ == 0) ? size_ - head_ - 1 : size_ - head_;
            if (space > 0)
                return {buf_.get() + head_, space};

            // Wrap-around fallback: try from the beginning
            if (tail_ > 1) {
                head_ = 0;
                size_t wrap_space = tail_ - 1;
                return {buf_.get(), wrap_space};
            }
        } else {
            // Case 2: tail is ahead of head, space is straightforward
            size_t space = tail_ - head_ - 1;
            if (space > 0)
                return {buf_.get() + head_, space};
        }

        // Buffer is full or cannot be safely wrapped
        return {nullptr, 0};
    }

    void RingBuffer::advance_head(size_t n) {
        head_ += n;
        last_write_time_ = ::esphome::millis();
    }

    size_t RingBuffer::available() const {
      return head_ - tail_;
    }

    size_t RingBuffer::free_space() const {
      return size_ - (head_ - tail_) - 1;  // Leave 1-byte gap to distinguish full/empty
    }

    void RingBuffer::clear() {
      head_ = tail_ = 0;
    }

    uint32_t RingBuffer::last_write_time() const {
      return last_write_time_;
    }

    size_t RingBuffer::index_(size_t pos) const {
      return pos % size_;
    }

  }  // namespace line_server
}  // namespace esphome