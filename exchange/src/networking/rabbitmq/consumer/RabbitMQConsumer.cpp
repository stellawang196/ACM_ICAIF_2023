#include "RabbitMQConsumer.hpp"

#include "networking/rabbitmq/connection_manager/RabbitMQConnectionManager.hpp"
#include "networking/rabbitmq/order_handler/RabbitMQOrderHandler.hpp"

namespace nutc {
namespace rabbitmq {

void
RabbitMQConsumer::handleIncomingMessages(
    manager::ClientManager& clients, engine_manager::Manager& engine_manager
)
{
    bool keepRunning = true;

    while (keepRunning) {
        auto incoming_message = consumeMessage();

        // Use std::visit to deal with the variant
        std::visit(
            [&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, messages::InitMessage>) {
                    log_e(rabbitmq, "Not expecting initialization message");
                    exit(1);
                }
                else if constexpr (std::is_same_v<T, messages::RMQError>) {
                    log_e(rabbitmq, "Received RMQError: {}", arg.message);
                }
                else if constexpr (std::is_same_v<T, messages::MarketOrder>) {
                    RabbitMQOrderHandler::handleIncomingMarketOrder(
                        engine_manager, clients, arg
                    );
                }
            },
            incoming_message
        );
    }
}

std::optional<std::string>
RabbitMQConsumer::consumeMessageAsString()
{
    const auto& connection_state =
        RabbitMQConnectionManager::getInstance().get_connection_state();

    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(connection_state);
    amqp_rpc_reply_t res = amqp_consume_message(connection_state, &envelope, NULL, 0);

    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        log_e(rabbitmq, "Failed to consume message.");
        return std::nullopt;
    }

    std::string message(
        reinterpret_cast<char*>(envelope.message.body.bytes), envelope.message.body.len
    );
    amqp_destroy_envelope(&envelope);
    return message;
}

std::variant<messages::InitMessage, messages::MarketOrder, messages::RMQError>
RabbitMQConsumer::consumeMessage()
{
    std::optional<std::string> buf = consumeMessageAsString();
    if (!buf.has_value()) {
        return messages::RMQError{"Failed to consume message."};
    }

    std::variant<messages::InitMessage, messages::MarketOrder, messages::RMQError> data;
    auto err = glz::read_json(data, buf.value());
    if (err) {
        return messages::RMQError{glz::format_error(err, buf.value())};
    }
    return data;
}

} // namespace rabbitmq
} // namespace nutc
