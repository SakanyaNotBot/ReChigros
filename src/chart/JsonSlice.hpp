#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rechigros::chart::json_slice {

inline bool skipString(std::string_view text, std::size_t& index) {
  if (index >= text.size() || text[index] != '"') return false;
  ++index;
  while (index < text.size()) {
    const char character = text[index];
    if (character == '\\') {
      index += 2;
      continue;
    }
    if (character == '"') return true;
    ++index;
  }
  return false;
}

inline bool skipValue(std::string_view text, std::size_t& index) {
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  if (index >= text.size()) return false;
  if (text[index] == '"') {
    if (!skipString(text, index)) return false;
    ++index;
    return true;
  }
  if (text[index] == '{' || text[index] == '[') {
    int depth = 0;
    while (index < text.size()) {
      const char character = text[index];
      if (character == '"') {
        if (!skipString(text, index)) return false;
        ++index;
        continue;
      }
      if (character == '{' || character == '[') {
        ++depth;
      } else if (character == '}' || character == ']') {
        --depth;
        if (depth == 0) {
          ++index;
          return true;
        }
      }
      ++index;
    }
    return false;
  }
  while (index < text.size()) {
    const char character = text[index];
    if (character == ',' || character == ']' || character == '}' ||
        std::isspace(static_cast<unsigned char>(character)) != 0) {
      break;
    }
    ++index;
  }
  return true;
}

inline bool findTopLevelValue(std::string_view text,
    std::initializer_list<std::string_view> keys, std::size_t& valueBegin,
    std::size_t& valueEnd) {
  std::size_t index = 0;
  int depth = 0;
  while (index < text.size()) {
    const char character = text[index];
    if (character == '"') {
      const std::size_t keyStart = index;
      if (!skipString(text, index)) return false;
      const std::size_t keyEnd = index;
      ++index;
      if (depth == 1) {
        std::size_t cursor = index;
        while (cursor < text.size() &&
            std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
          ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ':') {
          ++cursor;
          const std::string_view key = text.substr(keyStart + 1, keyEnd - keyStart - 1);
          for (const std::string_view wanted : keys) {
            if (key != wanted) continue;
            std::size_t begin = cursor;
            while (begin < text.size() &&
                std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
              ++begin;
            }
            std::size_t end = begin;
            if (!skipValue(text, end)) return false;
            valueBegin = begin;
            valueEnd = end;
            return true;
          }
        }
      }
      continue;
    }
    if (character == '{' || character == '[') {
      ++depth;
    } else if (character == '}' || character == ']') {
      if (depth == 0) return false;
      --depth;
    }
    ++index;
  }
  return false;
}

inline bool splitArray(std::string_view text, std::size_t begin, std::size_t end,
    std::vector<std::pair<std::size_t, std::size_t>>& elements) {
  if (begin >= end || text[begin] != '[' || text[end - 1] != ']') return false;
  const std::size_t closing = end - 1;
  std::size_t index = begin + 1;
  while (index < closing) {
    while (index < closing && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    if (index >= closing) break;
    const std::size_t elementBegin = index;
    if (!skipValue(text, index)) return false;
    std::size_t elementEnd = index;
    while (elementEnd > elementBegin &&
        std::isspace(static_cast<unsigned char>(text[elementEnd - 1])) != 0) {
      --elementEnd;
    }
    elements.emplace_back(elementBegin, elementEnd);
    while (index < closing && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    if (index < closing && text[index] == ',') {
      ++index;
      continue;
    }
    if (index < closing) return false;
  }
  return true;
}

inline std::string buildSkeleton(std::string_view text,
    std::vector<std::pair<std::size_t, std::size_t>> replacements) {
  std::sort(replacements.begin(), replacements.end(),
      [](const auto& first, const auto& second) { return first.first < second.first; });
  std::string skeleton;
  skeleton.reserve(text.size() / 32 + 1024);
  std::size_t cursor = 0;
  for (const auto& [begin, end] : replacements) {
    skeleton.append(text.substr(cursor, begin - cursor));
    skeleton += "[]";
    cursor = end;
  }
  skeleton.append(text.substr(cursor));
  return skeleton;
}

} // namespace rechigros::chart::json_slice
