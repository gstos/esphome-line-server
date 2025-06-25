#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "esphome/core/helpers.h"

class RingBuffer {
public:
  RingBuffer(size_t size, const std::string &terminator = "\r\n")
      : size_(size), buf_(new uint8_t[size]), terminator_(terminator) {}

  bool write(uint8_t byte) {
    if (free_space() == 0)
      return false;
    buf_[index_(head_++)] = byte;
    last_write_time_ = ::esphome::millis();
    return true;
  }

  size_t write_array(const uint8_t *data, size_t len) {
    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
      if (!write(data[i]))
        break;
      written++;
    }
    return written;
  }

  std::string read_line() {
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

  std::string flush_if_idle(uint32_t now, uint32_t timeout_ms) {
    if ((now - last_write_time_) >= timeout_ms && available() > 0) {
      std::string partial;
      for (size_t i = 0; i < (head_ - tail_); ++i)
        partial += static_cast<char>(buf_[index_(tail_ + i)]);
      tail_ = head_;
      return partial;
    }
    return "";
  }

  size_t available() const {
    return head_ - tail_;
  }

  size_t free_space() const {
    return size_ - (head_ - tail_);
  }

  void clear() {
    head_ = tail_ = 0;
  }

  uint32_t last_write_time() const {
    return last_write_time_;
  }

private:
  size_t index_(size_t pos) const {
    return pos % size_;
  }

  std::unique_ptr<uint8_t[]> buf_;
  size_t size_;
  size_t head_ = 0;
  size_t tail_ = 0;
  std::string terminator_;
  uint32_t last_write_time_ = 0;
};