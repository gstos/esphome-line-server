#include "line_server.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"
#include "esphome/core/version.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"

using esphome::line_server::RingBuffer;
using namespace esphome;

static const char *const TAG = "line_server";

void LineServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up line server...");

  // Ensure ring buffers are initialized if not set
  if (!this->uart_buf_) {
    this->uart_buf_ = std::unique_ptr<RingBuffer>(new RingBuffer(uart_buf_size_, uart_terminator_));
    ESP_LOGCONFIG(TAG, "UART buffer was not set explicitly. Using default size %zu, terminator '%s'",
             uart_buf_size_, uart_terminator_.c_str());
  }

  if (!this->tcp_buf_) {
    this->tcp_buf_ = std::unique_ptr<RingBuffer>(new RingBuffer(tcp_buf_size_, tcp_terminator_));
    ESP_LOGCONFIG(TAG, "TCP buffer was not set explicitly. Using default size %zu, terminator '%s'",
             tcp_buf_size_, tcp_terminator_.c_str());
  }

  // Setup TCP socket server
  struct sockaddr_storage bind_addr;
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2023, 4, 0)
  socklen_t bind_addrlen = socket::set_sockaddr_any(
      reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof(bind_addr), this->port_);
#else
  socklen_t bind_addrlen = socket::set_sockaddr_any(
      reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof(bind_addr), htons(this->port_));
#endif

  this->socket_ = socket::socket_ip(SOCK_STREAM, PF_INET);
  this->socket_->setblocking(false);
  this->socket_->bind(reinterpret_cast<struct sockaddr *>(&bind_addr), bind_addrlen);
  this->socket_->listen(8);

  this->publish_sensor();
}

void LineServerComponent::loop() {
  this->accept();
  this->read();                // UART → buffer
  this->flush_uart_buffer();   // UART → clients (on \r\n or timeout)
  this->write();               // TCP → buffer
  this->flush_tcp_buffer();    // TCP buffer → UART
  this->send_uart_keepalive(); // Keep-alive if needed (no clients connected)
  this->cleanup();
}

void LineServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Line Server:");
  ESP_LOGCONFIG(TAG, "- Listening on: %s:%u", esphome::network::get_use_address().c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "- UART buffer: size=%zu, terminator=%s",
      uart_buf_size_,
      esphome::format_hex_pretty((const uint8_t*)uart_terminator_.data(), uart_terminator_.size()).c_str());
ESP_LOGCONFIG(TAG, "- UART flush timeout: %ums", uart_flush_timeout_ms_);
ESP_LOGCONFIG(TAG, "- TCP buffer: size=%zu, terminator=%s",
      tcp_buf_size_,
      esphome::format_hex_pretty((const uint8_t*)tcp_terminator_.data(), tcp_terminator_.size()).c_str());
  ESP_LOGCONFIG(TAG, "- TCP flush timeout: %ums", tcp_flush_timeout_ms_);

#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Connected:", this->connected_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Connection count:", this->connection_count_sensor_);
#endif
}

void LineServerComponent::on_shutdown() {
  for (const Client &client : this->clients_)
    client.socket->shutdown(SHUT_RDWR);
}

void LineServerComponent::publish_sensor() {
#ifdef USE_BINARY_SENSOR
  if (this->connected_sensor_)
    this->connected_sensor_->publish_state(!this->clients_.empty());
#endif
#ifdef USE_SENSOR
  if (this->connection_count_sensor_)
    this->connection_count_sensor_->publish_state(this->clients_.size());
#endif
}

void LineServerComponent::accept() {
    struct sockaddr_storage client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    std::unique_ptr<socket::Socket> client_sock =
        this->socket_->accept(reinterpret_cast<struct sockaddr *>(&client_addr), &client_addrlen);
    if (!client_sock)
        return;

    if (!this->has_active_clients()) {
        ESP_LOGW(TAG, "No active clients connected, flushing UART RX buffer");
        if (this->uart_buf_)
            this->uart_buf_->clear();
        this->flush_uart_rx_buffer();
    }

    client_sock->setblocking(false);
    std::string identifier = client_sock->getpeername();
    this->clients_.emplace_back(std::move(client_sock), identifier);

    ESP_LOGD(TAG, "New client connected from %s", identifier.c_str());
    this->publish_sensor();
}

void LineServerComponent::cleanup() {
    auto active = [](const Client &c) { return !c.disconnected; };
    auto cutoff = std::partition(this->clients_.begin(), this->clients_.end(), active);
    if (cutoff != this->clients_.end()) {
        this->clients_.erase(cutoff, this->clients_.end());
        this->publish_sensor();
     }

     if (this->clients_.empty() && this->uart_busy_) {
         ESP_LOGW(TAG, "UART marked busy but no TCP clients — clearing flag");
         this->uart_busy_ = false;
     }
}

void LineServerComponent::read() {
    if (!this->uart_buf_)
        return;

    uint8_t temp[128];

    while (true) {
        int available = this->uart_bus_->available();
        if (available <= 0)
            break;

        uint8_t *chunk_ptr = temp;
        size_t chunk_size = sizeof(temp);
        bool write_to_ring = false;

        if (this->has_active_clients()) {
            auto chunk = this->uart_buf_->next_write_chunk();
            if (chunk.ptr == nullptr || chunk.size == 0)
                break;
            chunk_ptr = chunk.ptr;
            chunk_size = chunk.size;
            write_to_ring = true;
        }

        size_t read_len = std::min<size_t>(available, chunk_size);
        bool read_ok = this->uart_bus_->read_array(chunk_ptr, read_len);

        ESP_LOGD(TAG, "Read %zu bytes from UART of %d available", read_len, available);

        if (!read_ok) {
            ESP_LOGE(TAG, "UART read failed for %zu bytes", read_len);
            break;
        }

        if (write_to_ring) {
            this->uart_buf_->advance_head(read_len);
        } else {
            ESP_LOGV(TAG, "Discarded %zu bytes from UART (no clients connected)", read_len);
        }
    }
}

void LineServerComponent::flush_uart_buffer() {
    if (!this->uart_buf_)
        return;

    const uint32_t now = esphome::millis();

    // Flush full lines
    while (true) {
        std::string line = this->uart_buf_->read_line();
        if (line.empty())
            break;

        this->uart_busy_ = false;

        ESP_LOGD(TAG, "UART → TCP [line]: '%s'", line.c_str());
        for (Client &client : this->clients_) {
            if (!client.disconnected)
                client.socket->write(reinterpret_cast<const uint8_t *>(line.data()), line.size());
        }
    }

    // Handle stale partials
    if (this->uart_flush_timeout_ms_ > 0 &&
        (now - uart_buf_->last_write_time()) >= this->uart_flush_timeout_ms_ &&
        uart_buf_->available() > 0) {

        if (this->uart_timeout_callback_) {
            std::string partial = uart_buf_->read_line();  // Optional: switch to read_partial()
            std::string processed = this->uart_timeout_callback_(partial);

            if (!processed.empty()) {
                ESP_LOGW(TAG, "UART → TCP [timeout flush]: \'%s\'", processed.c_str());
                for (Client &client : this->clients_) {
                    if (!client.disconnected)
                        client.socket->write(reinterpret_cast<const uint8_t *>(processed.data()), processed.size());
                }
            } else {
                ESP_LOGW(TAG, "UART line timed out and was discarded by lambda");
            }
        } else {
            ESP_LOGW(TAG, "UART line timed out without terminator — discarding partial: size=%zu", uart_buf_->available());
        }
        this->uart_busy_ = false;
        uart_buf_->clear();  // Always clear after handling

        if (this->drop_on_uart_timeout_) {
            ESP_LOGW(TAG, "UART timeout — dropping TCP clients");
            for (auto &client : this->clients_) {
                if (!client.disconnected)
                    client.socket->close();
            }
        }
    }
}

void LineServerComponent::write() {
    if (!this->tcp_buf_)
        return;

    constexpr size_t buf_size = 128;
    uint8_t temp[buf_size];

    for (Client &client : this->clients_) {
        if (client.disconnected)
            continue;

        while (true) {
            ssize_t len = client.socket->read(temp, buf_size);
            if (len > 0) {
                size_t written = this->tcp_buf_->write_array(temp, len);
                if (written < static_cast<size_t>(len)) {
                    ESP_LOGW(TAG, "TCP buffer overflow — dropped %zu bytes", len - written);
                }
            } else if (len == 0 || errno == ECONNRESET) {
                ESP_LOGD(TAG, "Client %s disconnected during read", client.identifier.c_str());
                client.disconnected = true;
                break;
            } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;  // No more data available from this client
            } else {
                ESP_LOGW(TAG, "Error reading from client %s: errno=%d", client.identifier.c_str(), errno);
                client.disconnected = true;
                break;
            }
        }
    }
}

void LineServerComponent::flush_tcp_buffer() {
    if (!this->tcp_buf_ || this->uart_busy_)
        return;

    const uint32_t now = esphome::millis();

    // Step 1: send complete lines ending in terminator
    while (true) {
        std::string command = this->tcp_buf_->read_line();
        if (command.empty())
            break;

        ESP_LOGD(TAG, "TCP → UART [line]: '%s'", command.c_str());
        this->uart_busy_ = true;  // Prevent re-entrancy
        this->uart_bus_->write_array(reinterpret_cast<const uint8_t *>(command.data()), command.size());
    }

    // Step 2: handle stale partials
    if (this->tcp_flush_timeout_ms_ > 0 &&
        (now - tcp_buf_->last_write_time()) >= this->tcp_flush_timeout_ms_ &&
        tcp_buf_->available() > 0) {

        if (this->tcp_timeout_callback_) {
            std::string partial = tcp_buf_->read_partial();  // More appropriate than read_line()
            std::string processed = this->tcp_timeout_callback_(partial);

            if (!processed.empty()) {
                ESP_LOGW(TAG, "TCP → UART [timeout flush]: \"%s\"", processed.c_str());
                this->uart_bus_->write_array(reinterpret_cast<const uint8_t *>(processed.data()), processed.size());
            } else {
                ESP_LOGW(TAG, "TCP input timed out and was discarded by lambda");
            }
        } else {
            std::string partial = tcp_buf_->read_partial();
            ESP_LOGW(TAG, "TCP input timed out without terminator — discarding partial: size=%zu", partial.size());
        }

        tcp_buf_->clear();  // Always clear after timeout handling
    }
}

void LineServerComponent::send_uart_keepalive() {
    if (!this->clients_.empty() || this->keepalive_interval_ms_ == 0 || this->keepalive_message_.empty())
        return;

    uint32_t now = esphome::millis();
    if (now - this->last_keepalive_ < this->keepalive_interval_ms_)
        return;

    std::string msg = this->keepalive_message_;
    msg += this->tcp_terminator_;

    this->uart_bus_->write_array(reinterpret_cast<const uint8_t *>(msg.data()), msg.size());
    ESP_LOGD(TAG, "UART keep-alive sent: '%s'", msg.c_str());
    this->last_keepalive_ = now;
}

void LineServerComponent::flush_uart_rx_buffer() {
    uint8_t discard;
    int count = 0;
    while (this->uart_bus_->available() > 0) {
        if (this->uart_bus_->read_byte(&discard)) {
            count++;
        } else {
            break;
        }
    }

    if (count > 0)
        ESP_LOGD(TAG, "Flushed %d bytes from UART RX buffer", count);
}


bool LineServerComponent::has_active_clients() const {
  for (const auto &client : this->clients_) {
    if (!client.disconnected)
      return true;
  }
  return false;
}