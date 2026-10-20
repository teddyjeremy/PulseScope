#include "pulsescope/bookmarks.hpp"

namespace pulsescope {

Status BookmarkStore::Add(Bookmark bookmark) {
  if (bookmark.id.empty() || bookmark.title.empty()) {
    return Status{ErrorCode::InvalidArgument, "bookmark id and title are required"};
  }
  if (bookmark.end < bookmark.start) {
    return Status{ErrorCode::InvalidArgument, "bookmark end is before start"};
  }
  for (auto& item : items_) {
    if (item.id == bookmark.id) {
      item = std::move(bookmark);
      return Status::Ok();
    }
  }
  items_.push_back(std::move(bookmark));
  return Status::Ok();
}

bool BookmarkStore::Remove(std::string_view id) {
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    if (it->id == id) {
      items_.erase(it);
      return true;
    }
  }
  return false;
}

std::string BookmarkStore::ToJson() const {
  Json root = Json::Object();
  root.Set("schema", Json::String("pulsescope.bookmarks.v1"));
  std::vector<Json> arr;
  for (const auto& item : items_) {
    Json obj = Json::Object();
    obj.Set("id", Json::String(item.id));
    obj.Set("title", Json::String(item.title));
    obj.Set("start", Json::Uint(item.start.ticks));
    obj.Set("end", Json::Uint(item.end.ticks));
    obj.Set("note", Json::String(item.note));
    arr.push_back(std::move(obj));
  }
  root.Set("bookmarks", Json::Array(std::move(arr)));
  return StringifyJson(root, 2);
}

Status BookmarkStore::FromJson(std::string_view json, BookmarkStore& out) {
  Json root;
  Status st = ParseJson(json, root);
  if (!st) return st;
  const Json* arr = root.Find("bookmarks");
  if (arr == nullptr || arr->type != JsonType::Array) {
    return Status{ErrorCode::MalformedInput, "bookmarks array missing"};
  }
  BookmarkStore store;
  for (const auto& item : arr->a) {
    Bookmark b;
    const Json* id = item.Find("id");
    const Json* title = item.Find("title");
    const Json* start = item.Find("start");
    const Json* end = item.Find("end");
    const Json* note = item.Find("note");
    if (id == nullptr || id->AsString() == nullptr || title == nullptr || title->AsString() == nullptr) {
      return Status{ErrorCode::MalformedInput, "bookmark missing id or title"};
    }
    b.id = *id->AsString();
    b.title = *title->AsString();
    if (start != nullptr && start->AsUint()) b.start.ticks = *start->AsUint();
    if (end != nullptr && end->AsUint()) b.end.ticks = *end->AsUint();
    if (note != nullptr && note->AsString() != nullptr) b.note = *note->AsString();
    st = store.Add(std::move(b));
    if (!st) return st;
  }
  out = std::move(store);
  return Status::Ok();
}

}  // namespace pulsescope
