import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_BUFFER_SIZE,
    )

CONF_UART_BUFFER_SIZE = "uart_buffer_size"
CONF_UART_TERMINATOR = "uart_terminator"
CONF_UART_TIMEOUT = "uart_timeout"
CONF_UART_TIMEOUT_LAMBDA = "uart_timeout_lambda"
CONF_UART_TIMEOUT_DROP_CLIENTS = "uart_timeout_drop_clients"
CONF_UART_KEEPALIVE_INTERVAL = "uart_keepalive_interval"
CONF_UART_KEEPALIVE_MESSAGE = "uart_keepalive_message"

CONF_TCP_BUFFER_SIZE = "tcp_buffer_size"
CONF_TCP_TERMINATOR = "tcp_terminator"
CONF_TCP_TIMEOUT = "tcp_timeout"
CONF_TCP_TIMEOUT_LAMBDA = "tcp_timeout_lambda"

AUTO_LOAD = ["socket"]

DEPENDENCIES = ["uart", "network"]

MULTI_CONF = True

ns = cg.global_ns

LineServerComponent = ns.class_("LineServerComponent", cg.Component)


def validate_buffer_size(buffer_size):
    if buffer_size & (buffer_size - 1) != 0:
        raise cv.Invalid("Buffer size must be a power of two.")
    return buffer_size


def validate_terminator(value):
    value = cv.string(value)
    if len(value.encode("utf-8")) > 4:
        raise cv.Invalid("Terminator must be <= 4 bytes")
    return value


CONFIG_SCHEMA = cv.All(
    cv.require_esphome_version(2022, 3, 0),
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LineServerComponent),
            cv.Optional(CONF_PORT, default=6638): cv.port,

            cv.Optional(CONF_UART_BUFFER_SIZE, default=256): cv.All(
                cv.positive_int, validate_buffer_size
                ),
            cv.Optional(CONF_UART_TERMINATOR, default="\r\n"): validate_terminator,
            cv.Optional(CONF_UART_TIMEOUT, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_UART_TIMEOUT_LAMBDA): cv.returning_lambda,

            cv.Optional(CONF_TCP_BUFFER_SIZE, default=256): cv.All(
                cv.positive_int, validate_buffer_size
                ),
            cv.Optional(CONF_TCP_TERMINATOR, default="\r"): validate_terminator,
            cv.Optional(CONF_TCP_TIMEOUT, default="300ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TCP_TIMEOUT_LAMBDA): cv.returning_lambda,

            cv.Optional(CONF_UART_TIMEOUT_DROP_CLIENTS, default=False): cv.boolean,
            cv.Optional(CONF_UART_KEEPALIVE_INTERVAL, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_UART_KEEPALIVE_MESSAGE, default=""): cv.string,
            }
        )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_uart_buffer_size(config[CONF_UART_BUFFER_SIZE]))
    cg.add(var.set_tcp_buffer_size(config[CONF_TCP_BUFFER_SIZE]))
    cg.add(var.set_uart_terminator(config[CONF_UART_TERMINATOR]))
    cg.add(var.set_tcp_terminator(config[CONF_TCP_TERMINATOR]))
    cg.add(var.set_tcp_flush_timeout(config[CONF_TCP_TIMEOUT]))
    cg.add(var.set_uart_flush_timeout(config[CONF_UART_TIMEOUT]))
    cg.add(var.set_keepalive_message(config[CONF_UART_KEEPALIVE_MESSAGE]))
    cg.add(var.set_keepalive_interval(config[CONF_UART_KEEPALIVE_INTERVAL]))
    cg.add(var.set_drop_on_uart_timeout(config[CONF_UART_TIMEOUT_DROP_CLIENTS]))

    if CONF_UART_TIMEOUT_LAMBDA in config:
        uart_lambda_ = await cg.process_lambda(
            config[CONF_UART_TIMEOUT_LAMBDA],
            [],
            return_type=cg.std_string,
            )
        cg.add(var.set_uart_timeout_callback(uart_lambda_))

    if CONF_TCP_TIMEOUT_LAMBDA in config:
        tcp_lambda_ = await cg.process_lambda(
            config[CONF_TCP_TIMEOUT_LAMBDA],
            [],
            return_type=cg.std_string,
            )
        cg.add(var.set_tcp_timeout_callback(tcp_lambda_))

    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
