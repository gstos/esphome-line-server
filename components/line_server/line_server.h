#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/line_server/ring_buffer.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

class LineServerComponent : public esphome::Component {
public:
    LineServerComponent() = default;
    explicit LineServerComponent(esphome::uart::UARTComponent *stream) : stream_{stream} {}
    void set_uart_parent(esphome::uart::UARTComponent *parent) { this->stream_ = parent; }

    void set_uart_config(size_t size, const std::string &term) {
        uart_buf_size_ = size;
        uart_terminator_ = term;
    }

    void set_tcp_config(size_t size, const std::string &term) {
        tcp_buf_size_ = size;
        tcp_terminator_ = term;
    }

    void set_flush_timeout(uint32_t ms) {
        flush_timeout_ms_ = ms;
    }

    void set_uart_buffer_size(size_t size) { this->uart_buf_size_ = size; }
    void set_tcp_buffer_size(size_t size) { this->tcp_buf_size_ = size; }

    void set_uart_terminator(const std::string &term) { this->uart_terminator_ = term; }
    void set_tcp_terminator(const std::string &term) { this->tcp_terminator_ = term; }

#ifdef USE_BINARY_SENSOR
    void set_connected_sensor(esphome::binary_sensor::BinarySensor *connected) { this->connected_sensor_ = connected; }
#endif
#ifdef USE_SENSOR
    void set_connection_count_sensor(esphome::sensor::Sensor *connection_count) { this->connection_count_sensor_ = connection_count; }
#endif

    void setup() override;
    void loop() override;
    void dump_config() override;
    void on_shutdown() override;

    float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }

    void set_port(uint16_t port) { this->port_ = port; }

protected:
    void publish_sensor();

    void accept();
    void cleanup();
    void read();
    void flush_uart_buffer();
    void write();
    void flush_tcp_buffer();

    struct Client {
        Client(std::unique_ptr<esphome::socket::Socket> socket, std::string identifier)
            : socket(std::move(socket)), identifier{identifier} {}

        std::unique_ptr<esphome::socket::Socket> socket{nullptr};
        std::string identifier{};
        bool disconnected{false};
    };

    esphome::uart::UARTComponent *stream_{nullptr};
    uint16_t port_{};

    size_t uart_buf_size_ = 1024;
    std::string uart_terminator_ = "\r\n";

    size_t tcp_buf_size_ = 512;
    std::string tcp_terminator_ = "\r";

    uint32_t flush_timeout_ms_ = 100;

#ifdef USE_BINARY_SENSOR
    esphome::binary_sensor::BinarySensor *connected_sensor_ = nullptr;
#endif
#ifdef USE_SENSOR
    esphome::sensor::Sensor *connection_count_sensor_ = nullptr;
#endif

    std::unique_ptr<RingBuffer> uart_buf_;
    std::unique_ptr<RingBuffer> tcp_buf_;

    std::unique_ptr<esphome::socket::Socket> socket_;
    std::vector<Client> clients_;
};
