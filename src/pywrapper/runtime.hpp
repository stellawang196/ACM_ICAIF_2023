#pragma once

#include "logging.hpp"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

namespace nutc {
namespace pywrapper {
void init(std::function<bool(const std::string&, float, bool, const std::string&, float)>
              publish_market_order);
void
create_api_module(std::function<bool(const std::string&, float, bool, const std::string&, float)>
                      publish_market_order);
void run_code_init(const std::string& py_code);
} // namespace pywrapper
} // namespace nutc