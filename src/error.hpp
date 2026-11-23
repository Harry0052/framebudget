// SPDX-License-Identifier: MIT
#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace fb {

// category is a stable machine token. what() is the actionable message.
class Error : public std::runtime_error {
 public:
  std::string category;

  Error(std::string category_in, std::string message)
      : std::runtime_error(std::move(message)), category(std::move(category_in)) {}
};

}  // namespace fb
