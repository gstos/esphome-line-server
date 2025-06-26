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
      size_t scan = tail_;
      while (scan + terminator_.size() <= head_) {
        bool match = true;
        for (size_t i = 0; i < terminator_.size(); ++i) {
          if (buf_[index_(scan + i)] != terminator_[i]) {
            match = false;
            break;
          }
        }
        if (match) {
          size_t len = scan + terminator_.size() - tail_;
          std::string line;
          for (size_t i = 0; i < len; ++i)
            line += static_cast<char>(buf_[index_(tail_ + i)]);
          tail_ += len;
          return line;
        }
        scan++;
      }
      return "";
    }

    std::string RingBuffer::flush_if_idle(uint32_t now, uint32_t timeout_ms) {
      if ((now - last_write_time_) >= timeout_ms && available() > 0) {
        std::string partial;
        for (size_t i = 0; i < (head_ - tail_); ++i)
          partial += static_cast<char>(buf_[index_(tail_ + i)]);
        tail_ = head_;
        return partial;
      }
      return "";
    }

    std::pair<uint8_t*, size_t> RingBuffer::next_free_ptr_and_size() {
        if (head_ >= tail_) {
        	// Space from head_ to end, or up to tail_ if buffer not full
        	size_t space = (tail_ == 0) ? size_ - head_ - 1 : size_ - head_;
        	if (space == 0) return {nullptr, 0};
        	return {buf_.get() + head_, space};
    	} else {
        	// Space from head_ to tail_ - 1
        	size_t space = tail_ - head_ - 1;
        	if (space == 0) return {nullptr, 0};
        	return {buf_.get() + head_, space};
    	}
	}

    size_t RingBuffer::available() const {
      return head_ - tail_;
    }

    size_t RingBuffer::free_space() const {
      return size_ - (head_ - tail_);
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