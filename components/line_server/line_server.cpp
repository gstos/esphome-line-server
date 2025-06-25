#include "line_server.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"
#include "esphome/core/version.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"

static const char *const TAG = "line_server";

using namespace esphome;

void LineServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up line server...");

  // Ensure ring buffers are initialized if not set
  if (!this->uart_buf_) {
    this->uart_buf_ = std::make_unique<RingBuffer>(uart_buf_size_, uart_terminator_);
    ESP_LOGW(TAG, "UART buffer was not set explicitly. Using default size %zu, terminator '%s'",
             uart_buf_size_, uart_terminator_.c_str());
  }

  if (!this->tcp_buf_) {
    this->tcp_buf_ = std::make_unique<RingBuffer>(tcp_buf_size_, tcp_terminator_);
    ESP_LOGW(TAG, "TCP buffer was not set explicitly. Using default size %zu, terminator '%s'",
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
  this->read();              // UART → buffer
  this->flush_uart_buffer(); // UART → clients (on \r\n or timeout)
  this->write();             // TCP → buffer
  this->flush_tcp_buffer();  // TCP buffer → UART
  this->cleanup();
}

void LineServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Line Server:");
  ESP_LOGCONFIG(TAG, "  Listening on: %s:%u", esphome::network::get_use_address().c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "UART buffer: size=%zu, terminator=%s",
      uart_buf_size_,
      esphome::format_hex_pretty((const uint8_t*)uart_terminator_.data(), uart_terminator_.size()).c_str());
  ESP_LOGCONFIG(TAG, "TCP buffer: size=%zu, terminator=%s",
      tcp_buf_size_,
      esphome::format_hex_pretty((const uint8_t*)tcp_terminator_.data(), tcp_terminator_.size()).c_str());
  ESP_LOGCONFIG(TAG, "  Flush timeout: %ums", flush_timeout_ms_);

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
}

void LineServerComponent::read() {
    if (!this->stream_ || !this->uart_buf_)
        return;

    constexpr size_t chunk_size = 128;
    uint8_t buf[chunk_size];

    int available = this->stream_->available();
    if (available <= 0)
        return;

    size_t read_len = this->stream_->read_array(buf, std::min(available, static_cast<int>(chunk_size)));
    ESP_LOGD(TAG, "Read %zu bytes from UART", read_len);

    if (read_len > 0) {
        size_t written = this->uart_buf_->write_array(buf, read_len);
        if (written < read_len) {
            ESP_LOGW(TAG, "UART ring buffer overflow — dropped %zu bytes", read_len - written);
        }
    }
}

void LineServerComponent::flush_uart_buffer() {
    if (!this->uart_buf_)
        return;

    const uint32_t now = millis();

    // Flush full lines
    while (true) {
        std::string line = this->uart_buf_->read_line();
        if (line.empty())
            break;

        ESP_LOGD(TAG, "UART → TCP [line]: \"%s\"", line.c_str());
        for (Client &client : this->clients_) {
            if (!client.disconnected)
                client.socket->write(reinterpret_cast<const uint8_t *>(line.data()), line.size());
        }
    }

    // Log and discard stale partials (but do NOT flush)
    if ((now - uart_buf_->last_write_time()) >= this->flush_timeout_ms_ && uart_buf_->available() > 0) {
        ESP_LOGW(TAG, "UART line timed out without terminator — discarding partial: size=%zu", uart_buf_->available());
        uart_buf_->clear();  // optional: or let it sit
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
    if (!this->tcp_buf_ || !this->stream_)
        return;

    const uint32_t now = millis();

    // Step 1: send complete lines ending in \r
    while (true) {
        std::string command = this->tcp_buf_->read_line();
        if (command.empty())
            break;

        ESP_LOGD(TAG, "TCP → UART [line]: \"%s\"", command.c_str());

        this->stream_->write_array(reinterpret_cast<const uint8_t *>(command.data()), command.size());
    }

    // Step 2: flush partial command if idle
    std::string partial = this->tcp_buf_->flush_if_idle(now, this->flush_timeout_ms_);
    if (!partial.empty()) {
        ESP_LOGW(TAG, "TCP → UART [timeout flush]: \"%s\"", partial.c_str());
        this->stream_->write_array(reinterpret_cast<const uint8_t *>(partial.data()), partial.size());
    }
}

LineServerComponent::Client::Client(std::unique_ptr<esphome::socket::Socket> socket, std::string identifier)
  : socket(std::move(socket)), identifier(std::move(identifier)) {}
