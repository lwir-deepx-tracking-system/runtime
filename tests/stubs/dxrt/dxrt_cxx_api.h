#pragma once
// Host-only tensor shim for the postprocessor test. It never runs inference.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
namespace dxrt {
enum class DataType { FLOAT };
class Tensor {
    std::vector<int64_t> shape_;
    std::vector<unsigned char> bytes_;
public:
    Tensor(std::string, int, std::vector<int64_t> shape, const void* data,
           std::size_t bytes, bool) : shape_(std::move(shape)), bytes_(bytes) {
        if (bytes) std::memcpy(bytes_.data(), data, bytes);
    }
    DataType type() const { return DataType::FLOAT; }
    const std::vector<int64_t>& shape() const { return shape_; }
    const void* data() const { return bytes_.data(); }
    std::size_t size_in_bytes() const { return bytes_.size(); }
};
using TensorPtr = std::shared_ptr<Tensor>;
using TensorPtrs = std::vector<TensorPtr>;
}
