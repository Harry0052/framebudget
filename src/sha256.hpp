// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fb {

class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t len);
  std::string hex();

 private:
  void transform(const std::uint8_t block[64]);
  std::uint32_t state_[8]{};
  std::uint64_t bytes_ = 0;
  std::uint8_t buffer_[64]{};
  std::size_t buffer_len_ = 0;
  bool finished_ = false;
};

std::string sha256_bytes(const void* data, std::size_t len);
std::string sha256_file(const std::string& path);

}  // namespace fb
