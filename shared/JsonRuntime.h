#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Nganu {
namespace JsonRuntime {

namespace detail {

inline void SkipWhitespace(std::string_view text, size_t& index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
}

inline bool ParseString(std::string_view text, size_t& index, std::string& out) {
    if (index >= text.size() || text[index] != '"') {
        return false;
    }
    ++index;
    out.clear();
    while (index < text.size()) {
        const char ch = text[index++];
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (index >= text.size()) {
                return false;
            }
            const char esc = text[index++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default:
                return false;
            }
            continue;
        }
        out.push_back(ch);
    }
    return false;
}

inline bool SkipNested(std::string_view text, size_t& index, char open, char close) {
    if (index >= text.size() || text[index] != open) {
        return false;
    }
    int depth = 0;
    while (index < text.size()) {
        const char ch = text[index++];
        if (ch == '"') {
            --index;
            std::string ignored;
            if (!ParseString(text, index, ignored)) {
                return false;
            }
            continue;
        }
        if (ch == open) {
            ++depth;
        } else if (ch == close) {
            --depth;
            if (depth == 0) {
                return true;
            }
        }
    }
    return false;
}

inline bool ParseValue(std::string_view text, size_t& index, std::string& out) {
    SkipWhitespace(text, index);
    if (index >= text.size()) {
        return false;
    }
    if (text[index] == '"') {
        return ParseString(text, index, out);
    }
    if (text[index] == '{') {
        const size_t start = index;
        if (!SkipNested(text, index, '{', '}')) {
            return false;
        }
        out.assign(text.substr(start, index - start));
        return true;
    }
    if (text[index] == '[') {
        const size_t start = index;
        if (!SkipNested(text, index, '[', ']')) {
            return false;
        }
        out.assign(text.substr(start, index - start));
        return true;
    }
    const size_t start = index;
    while (index < text.size() && text[index] != ',' && text[index] != '}' && text[index] != ']') {
        ++index;
    }
    size_t end = index;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    out.assign(text.substr(start, end - start));
    return !out.empty();
}

} // namespace detail

inline std::vector<std::string> SplitTopLevelObjects(const std::string& json) {
    std::vector<std::string> objects;
    size_t i = 0;
    detail::SkipWhitespace(json, i);
    if (i >= json.size() || json[i] != '[') {
        return objects;
    }
    ++i;
    while (i < json.size()) {
        detail::SkipWhitespace(json, i);
        if (i < json.size() && json[i] == ']') {
            break;
        }
        if (i >= json.size() || json[i] != '{') {
            break;
        }
        const size_t start = i;
        if (!detail::SkipNested(json, i, '{', '}')) {
            break;
        }
        objects.emplace_back(json.substr(start, i - start));
        detail::SkipWhitespace(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
        }
    }
    return objects;
}

inline std::unordered_map<std::string, std::string> ParseFlatObject(const std::string& json) {
    std::unordered_map<std::string, std::string> fields;
    size_t i = 0;
    detail::SkipWhitespace(json, i);
    if (i >= json.size() || json[i] != '{') {
        return fields;
    }
    ++i;
    while (i < json.size()) {
        detail::SkipWhitespace(json, i);
        if (i < json.size() && json[i] == '}') {
            break;
        }
        std::string key;
        if (!detail::ParseString(json, i, key)) {
            break;
        }
        detail::SkipWhitespace(json, i);
        if (i >= json.size() || json[i] != ':') {
            break;
        }
        ++i;
        std::string value;
        if (!detail::ParseValue(json, i, value)) {
            break;
        }
        fields[key] = value;
        detail::SkipWhitespace(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == '}') {
            break;
        }
    }
    return fields;
}

inline std::optional<std::string> GetString(const std::unordered_map<std::string, std::string>& fields, const std::string& key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return std::nullopt;
    }
    return it->second;
}

inline std::optional<int> GetInt(const std::unordered_map<std::string, std::string>& fields, const std::string& key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        const int value = std::stoi(it->second, &consumed);
        if (consumed != it->second.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<float> GetFloat(const std::unordered_map<std::string, std::string>& fields, const std::string& key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        const float value = std::stof(it->second, &consumed);
        if (consumed != it->second.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<bool> GetBool(const std::unordered_map<std::string, std::string>& fields, const std::string& key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return std::nullopt;
    }
    if (it->second == "true") {
        return true;
    }
    if (it->second == "false") {
        return false;
    }
    return std::nullopt;
}

} // namespace JsonRuntime
} // namespace Nganu
