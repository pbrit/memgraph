#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "storage/v2/property_value.hpp"
#include "utils/logging.hpp"

/**
 * Encapsulates user provided parameters (and stripped literals)
 * and provides ways of obtaining them by position.
 */
namespace query {

struct Parameters {
 public:
  /**
   * Adds a value to the stripped arguments under a token position.
   *
   * @param position Token position in query of value.
   * @param value
   */
  void Add(int position, const storage::PropertyValue &value) {
    storage_.emplace_back(position, value);
  }

  /**
   *  Returns the value found for the given token position.
   *
   *  @param position Token position in query of value.
   *  @return Value for the given token position.
   */
  const storage::PropertyValue &AtTokenPosition(int position) const {
    auto found =
        std::find_if(storage_.begin(), storage_.end(),
                     [&](const auto &a) { return a.first == position; });
    MG_ASSERT(found != storage_.end(),
              "Token position must be present in container");
    return found->second;
  }

  /**
   * Returns the position-th stripped value. Asserts that this
   * container has at least (position + 1) elements.
   *
   * @param position Which stripped param is sought.
   * @return Token position and value for sought param.
   */
  const std::pair<int, storage::PropertyValue> &At(int position) const {
    MG_ASSERT(position < static_cast<int>(storage_.size()), "Invalid position");
    return storage_[position];
  }

  /** Returns the number of arguments in this container */
  auto size() const { return storage_.size(); }

  auto begin() const { return storage_.begin(); }
  auto end() const { return storage_.end(); }

 private:
  std::vector<std::pair<int, storage::PropertyValue>> storage_;
};

}  // namespace query
