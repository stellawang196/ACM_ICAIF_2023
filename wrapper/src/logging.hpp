#pragma once

#include "config.h"

#include <quill/Quill.h>

#include <string>

namespace nutc {
namespace logging {

#if DEBUG() // Debug mode
static constexpr quill::LogLevel DEFAULT_LOG_LEVEL = quill::LogLevel::Debug;
#else // Release mode
static constexpr quill::LogLevel DEFAULT_LOG_LEVEL = quill::LogLevel::Info;
#endif

namespace detail {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline quill::LogLevel application_log_level;

inline quill::Logger*
get_logger(const std::string& name)
{
    quill::Logger* logger = nullptr;

    try {
        logger = quill::get_logger(name.c_str());
    } catch (quill::QuillError&) {
        logger = quill::create_logger(name);
        logger->set_log_level(application_log_level);
        logger->init_backtrace(LOG_BACKTRACE_SIZE, quill::LogLevel::Error);
    }

    return logger;
}

} // namespace detail

/**
 * Set our thread name.
 */
inline void
set_thread_name(const std::string& name)
{
    quill::detail::set_thread_name(name.c_str());
}

/**
 * Set up logging.
 */
void init(quill::LogLevel log_level = DEFAULT_LOG_LEVEL, const std::string& uid = "");

/**
 * Set up logging.
 */
inline void
init(uint8_t verbosity = 0, const std::string& uid = "")
{
    auto log_level = static_cast<uint8_t>(DEFAULT_LOG_LEVEL);

    if (verbosity <= log_level)
        log_level -= verbosity;
    else // protect from underflow
        log_level = 0;

    init(static_cast<quill::LogLevel>(log_level), uid);
}

/******************************************************************************
 *                                 LOGGERS
 *****************************************************************************/
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

inline quill::Logger*
get_main_logger()
{
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static auto* logger = quill::get_root_logger();
    return logger;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CREATE_LOG_CATEGORY(category)                                                  \
    inline quill::Logger* get_##category##_logger()                                    \
    {                                                                                  \
        static auto* logger = detail::get_logger(#category);                           \
        return logger;                                                                 \
    }                                                                                  \
    class ____dummy_##category // makes you add a semicolon

// Create loggers here for every category
CREATE_LOG_CATEGORY(py_runtime);
CREATE_LOG_CATEGORY(redis);
CREATE_LOG_CATEGORY(web);
CREATE_LOG_CATEGORY(libcurl);
CREATE_LOG_CATEGORY(rabbitmq);
CREATE_LOG_CATEGORY(firebase);

#undef CREATE_LOG_CATEGORY
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace logging
} // namespace nutc

// NOLINTBEGIN
#define log_bt(category, ...)                                                          \
    LOG_BACKTRACE(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_t3(category, ...)                                                          \
    LOG_TRACE_L3(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_t2(category, ...)                                                          \
    LOG_TRACE_L2(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_t1(category, ...)                                                          \
    LOG_TRACE_L1(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_d(category, ...)                                                           \
    LOG_DEBUG(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_i(category, ...)                                                           \
    LOG_INFO(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_w(category, ...)                                                           \
    LOG_WARNING(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_e(category, ...)                                                           \
    LOG_ERROR(nutc::logging::get_##category##_logger(), __VA_ARGS__)

#define log_c(category, ...)                                                           \
    LOG_CRITICAL(nutc::logging::get_##category##_logger(), __VA_ARGS__)
// NOLINTEND
