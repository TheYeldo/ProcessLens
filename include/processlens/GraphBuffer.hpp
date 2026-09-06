#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace processlens {

class GraphBuffer {
public:
    explicit GraphBuffer(std::size_t capacity = 120)
        : values_(std::max<std::size_t>(capacity, 1), 0.0f) {}

    void Push(float value) noexcept {
        values_[writeIndex_] = value;
        writeIndex_ = (writeIndex_ + 1) % values_.size();
        count_ = std::min(count_ + 1, values_.size());
    }

    [[nodiscard]] std::vector<float> Values() const {
        std::vector<float> result;
        result.reserve(count_);
        const std::size_t start = count_ == values_.size() ? writeIndex_ : 0;
        for (std::size_t i = 0; i < count_; ++i) {
            result.push_back(values_[(start + i) % values_.size()]);
        }
        return result;
    }

    [[nodiscard]] std::size_t Size() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return values_.size(); }

private:
    std::vector<float> values_;
    std::size_t writeIndex_{};
    std::size_t count_{};
};

} // namespace processlens
