#include "rabbitmq.hpp"

#include "logging.hpp"

namespace nutc {
namespace rabbitmq {
bool
RabbitMQ::logAndReturnError(const char* errorMessage)
{
    log_e(rabbitmq, "{}", errorMessage);
    return false;
}

bool
RabbitMQ::connectToRabbitMQ(
    const std::string& hostname, int port, const std::string& username,
    const std::string& password
)
{
    conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(conn);

    if (!socket)
        return logAndReturnError("Cannot create TCP socket");
    if (amqp_socket_open(socket, hostname.c_str(), port))
        return logAndReturnError("Cannot open socket");

    amqp_rpc_reply_t reply = amqp_login(
        conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, username.c_str(),
        password.c_str()
    );
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        return logAndReturnError("Login failed");

    return true;
}

bool
RabbitMQ::initializeQueue(const std::string& queueName)
{
    amqp_queue_declare(
        conn, 1, amqp_cstring_bytes(queueName.c_str()), 0, 0, 0, 1, amqp_empty_table
    );
    amqp_rpc_reply_t res = amqp_get_rpc_reply(conn);

    if (res.reply_type != AMQP_RESPONSE_NORMAL)
        return logAndReturnError("Failed to declare queue.");
    log_i(rabbitmq, "Declared queue: {}", queueName);

    return true;
}

bool
RabbitMQ::initializeConsume(const std::string& queueName)
{
    amqp_basic_consume(
        conn, 1, amqp_cstring_bytes(queueName.c_str()), amqp_empty_bytes, 0, 1, 0,
        amqp_empty_table
    );

    amqp_rpc_reply_t res = amqp_get_rpc_reply(conn);
    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        log_e(rabbitmq, "Failed to consume message.");
        return false;
    }

    return true;
}

void
RabbitMQ::handleIncomingMessages(nutc::matching::Engine& engine)
{
    // need condition for closing
    while (true) {
        std::variant<InitMessage, MarketOrder, RMQError> incoming_message =
            consumeMessage();
        bool is_init = std::holds_alternative<InitMessage>(incoming_message);
        bool is_mo = std::holds_alternative<MarketOrder>(incoming_message);
        bool is_error = std::holds_alternative<RMQError>(incoming_message);
        if (is_init) [[unlikely]] {
            log_e(rabbitmq, "Not expecting initialization message");
            exit(1);
        }
        if (is_error) [[unlikely]] {
            RMQError err = std::get<RMQError>(incoming_message);
            log_e(rabbitmq, "Received RMQError: {}", err.message);
        }
        if (is_mo) [[likely]] {
            MarketOrder order = std::get<MarketOrder>(incoming_message);
            std::string buffer;
            glz::write<glz::opts{}>(order, buffer);

            log_i(rabbitmq, "Received market order: {}", buffer);
            // TODO: these should not be two different classes
            nutc::matching::Order newMO{
                order.ticker, "MARKET", order.side == messages::BUY, order.quantity,
                order.price};
            engine.add_order(newMO);
        }
    }
}

bool
RabbitMQ::publishMessage(const std::string& queueName, const std::string& message)
{
    amqp_rpc_reply_t res = amqp_get_rpc_reply(conn);
    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        log_e(rabbitmq, "Failed to declare queue.");
        return false;
    }

    amqp_basic_publish(
        conn, 1, amqp_cstring_bytes(""), amqp_cstring_bytes(queueName.c_str()), 0, 0,
        NULL, amqp_cstring_bytes(message.c_str())
    );

    res = amqp_get_rpc_reply(conn);
    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        log_e(rabbitmq, "Failed to publish message.");
        return false;
    }

    return true;
}

// Blocking
std::string
RabbitMQ::consumeMessageAsString()
{
    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(conn);
    amqp_rpc_reply_t res = amqp_consume_message(conn, &envelope, NULL, 0);

    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        log_e(rabbitmq, "Failed to consume message.");
        return "";
    }

    std::string message(
        reinterpret_cast<char*>(envelope.message.body.bytes), envelope.message.body.len
    );
    amqp_destroy_envelope(&envelope);
    return message;
}

std::variant<InitMessage, MarketOrder, RMQError>
RabbitMQ::consumeMessage()
{
    std::string buf = consumeMessageAsString();
    if (buf == "") {
        return RMQError{"Failed to consume message."};
    }

    std::variant<InitMessage, MarketOrder, RMQError> data{};
    auto err = glz::read_json(data, buf);
    if (err) {
        std::string error = glz::format_error(err, buf);
        return RMQError{error};
    }
    return data;
}

// todo: find way to prevent clients from starting until all are ready
void
RabbitMQ::waitForClients(int num_clients, nutc::manager::ClientManager& clients)
{
    for (int i = 0; i < num_clients; i++) {
        std::variant<InitMessage, MarketOrder, RMQError> data = consumeMessage();
        if (std::holds_alternative<RMQError>(data)) {
            std::string error = std::get<RMQError>(data).message;
            log_e(rabbitmq, "Failed to consume message with error {}.", error);
            return;
        }
        if (std::holds_alternative<MarketOrder>(data)) {
            log_i(
                rabbitmq,
                "Received market order before initialization complete. Ignoring..."
            );
            continue;
        }
        InitMessage init = std::get<InitMessage>(data);
        log_i(
            rabbitmq, "Received init message from client {} with status {}",
            init.client_uid, init.ready ? "ready" : "not ready"
        );
        if (init.ready) {
            clients.setClientActive(init.client_uid);
        }
    }
    log_i(rabbitmq, "All clients ready. Starting exchange");
}

bool
RabbitMQ::initializeConnection()
{
    if (!connectToRabbitMQ("localhost", 5672, "NUFT", "ADMIN"))
        return false;

    amqp_channel_open(conn, 1);
    amqp_rpc_reply_t res = amqp_get_rpc_reply(conn);
    if (res.reply_type != AMQP_RESPONSE_NORMAL)
        return logAndReturnError("Failed to open channel.");

    if (!initializeQueue("market_order"))
        return false;
    if (!initializeConsume("market_order"))
        return false;

    return true;
}

void
RabbitMQ::closeConnection(const nutc::manager::ClientManager& users)
{
    for (auto& [uid, active] : users.getClients(true)) {
        log_i(rabbitmq, "Shutting down client {}", uid);
        ShutdownMessage shutdown{uid};
        std::string mess = glz::write_json(shutdown);
        publishMessage(uid, mess);
    }
    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
}

} // namespace rabbitmq
} // namespace nutc
