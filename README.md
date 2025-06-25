# Line Server for ESPHome

**LineServer** is a custom ESPHome component based on [@oxan’s stream_server](https://github.com/oxan/esphome-stream-server). 
It acts as a transparent UART-to-TCP line-oriented bridge, 
with extended support for line terminators, timeouts, and directional buffers. 
This makes it ideal for RS-232-based command protocols like RIO (Russound), 
which use line-based communication patterns.

The component listens on a configurable TCP port and forwards lines between UART and TCP clients, 
flushing on a terminator or idle timeout.

---

## Features

- Bi-directional UART–TCP communication
- Independent ring buffers for UART and TCP
- Configurable line terminators for each direction
- Idle flush timeout to handle incomplete lines
- TCP client tracking with optional sensors
- Multiple UARTs supported
- Compatible with Wi-Fi and Ethernet

---

## Requirements

- ESPHome version **2022.3.0** or newer

---

## Installation

```yaml
external_components:
  - source: github://gstos/esphome-line-server
    components: [line_server]
```

## Basic Usage

```yaml
uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

line_server:
  uart_id: uart_bus


## Configuration Options

| Key                  | Type            | Default | Description                                               |
|-----------------------|------------------|---------|-----------------------------------------------------------|
| `port`                | integer           | `6638`  | TCP server port                                           |
| `uart_buffer_size`    | power of 2 int    | `256`   | Buffer size for UART input                                |
| `tcp_buffer_size`     | power of 2 int    | `256`   | Buffer size for TCP input                                 |
| `uart_terminator`     | string            | `"\r\n"`| Terminator to flush UART buffer to TCP                    |
| `tcp_terminator`      | string            | `"\r"`  | Terminator to flush TCP buffer to UART                    |
| `timeout`             | duration          | `300ms` | Time before incomplete messages are flushed               |

### Example with all options:

```yaml
line_server:
  uart_id: uart_bus
  port: 7000
  uart_buffer_size: 512
  tcp_buffer_size: 512
  uart_terminator: "\r\n"
  tcp_terminator: "\r"
  timeout: 500ms

## Sensors

### Binary Sensor: Client Connected

```yaml
binary_sensor:
  - platform: line_server
    connected:
      name: TCP Client Connected

### Sensor: Connection Count
```yaml
sensor:
  - platform: line_server
    connections:
      name: TCP Client Count
```

## Multiple UARTs

You can use multiple UARTs with separate line servers:

```yaml
uart:
  - id: uart1
    rx_pin: GPIO16
    tx_pin: GPIO17
    baud_rate: 9600

  - id: uart2
    rx_pin: GPIO25
    tx_pin: GPIO26
    baud_rate: 19200

line_server:
  - uart_id: uart1
    port: 7001

  - uart_id: uart2
    port: 7002
```

## Notes

- Buffer sizes must be **powers of two**.
- Terminators must be **≤ 4 bytes**, UTF-8 encoded.
- All data is treated as **raw** — no Telnet, RFC2217, or control sequences.
- Originally based on [esphome-stream-server](https://github.com/oxan/esphome-stream-server) by @oxan.

