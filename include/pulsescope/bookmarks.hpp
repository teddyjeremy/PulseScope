#pragma once

#include "pulsescope/json.hpp"
#include "pulsescope/timestamp.hpp"

#include <string>
#include <vector>

namespace pulsescope {

struct Bookmark {
  std::string id;
  std::string title;
  Timestamp start{};
  Timestamp end{};
  std::string note;
};

class BookmarkStore {
 public:
  Status Add(Bookmark bookmark);
  bool Remove(std::string_view id);
  const std::vector<Bookmark>& All() const { return items_; }
  std::string ToJson() const;
  static Status FromJson(std::string_view json, BookmarkStore& out);

 private:
  std::vector<Bookmark> items_;
};

}  // namespace pulsescope
