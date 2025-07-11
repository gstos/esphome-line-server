#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace esphome {
    namespace line_server {

        class RingBuffer {
        public:
            RingBuffer(size_t size, const std::string &terminator = "\r\n");

            bool write(uint8_t byte);
            size_t write_array(const uint8_t *data, size_t len);
            std::string read_line();
            std::string read_partial();
            std::string flush_if_idle(uint32_t now, uint32_t timeout_ms);
            size_t available() const;
            size_t free_space() const;
            void clear();
            uint32_t last_write_time() const;

            struct BufferSlice {
                uint8_t* ptr;
                size_t size;
            };
            BufferSlice next_write_chunk();
            void advance_head(size_t n);

            bool is_empty() const { return head_ == tail_; }
            bool is_full() const { return (head_ + 1) % size_ == tail_; }

        private:
            size_t index_(size_t pos) const;

            std::unique_ptr<uint8_t[]> buf_;
            size_t size_;
            size_t head_ = 0;
            size_t tail_ = 0;
            std::string terminator_;
            uint32_t last_write_time_ = 0;
        };

    }  // namespace line_server
}  // namespace esphome
