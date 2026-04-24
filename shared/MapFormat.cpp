#include "MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace Nganu {
namespace MapFormat {
namespace {

struct XmlNode {
    std::string name;
    std::unordered_map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
};

ParseResult Fail(int line, std::string error) {
    ParseResult result;
    result.line = line;
    result.error = std::move(error);
    return result;
}

std::string TrimCopy(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

void AddAssetRefForDomain(std::vector<std::string>& mapRefs,
                          std::vector<std::string>& characterRefs,
                          const std::string& ref,
                          AssetDomain fallbackDomain) {
    const std::string file = AssetFileName(ref);
    if (file.empty()) {
        return;
    }
    const AssetDomain domain = AssetDomainForRef(ref, fallbackDomain);
    AddUniqueAssetRef(domain == AssetDomain::Character ? characterRefs : mapRefs, file);
}

std::string DomainPrefix(AssetDomain domain) {
    return domain == AssetDomain::Character ? "character:" : "map:";
}

std::optional<std::string> ResolveAssetRef(const std::string& value,
                                           AssetDomain fallbackDomain,
                                           const std::unordered_map<std::string, std::pair<AssetDomain, std::string>>& assets) {
    const std::vector<std::string> parts = SplitEscaped(value, '@');
    if (parts.size() == 5) {
        auto assetIt = assets.find(parts[0]);
        if (assetIt != assets.end()) {
            return DomainPrefix(assetIt->second.first) + assetIt->second.second + "@" + parts[1] + "@" + parts[2] + "@" + parts[3] + "@" + parts[4];
        }
    }

    if (ParseAtlasRef(value).has_value()) {
        return value;
    }

    if (parts.size() != 5) {
        return std::nullopt;
    }

    if (parts[0].find(".png") != std::string::npos) {
        return DomainPrefix(fallbackDomain) + value;
    }

    return std::nullopt;
}

bool AppendTile(Document& document, const std::string& layer, int x, int y, const std::string& asset) {
    if (layer.empty() || asset.empty()) {
        return false;
    }
    document.stamps.push_back(Stamp {layer, x, y, asset});
    return true;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ParseLocalRect(const std::string& value, Rect& out) {
    const std::vector<std::string> parts = SplitEscaped(value, '|');
    if (parts.size() != 4) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) &&
           ParseFloatStrict(parts[1], out.y) &&
           ParseFloatStrict(parts[2], out.width) &&
           ParseFloatStrict(parts[3], out.height) &&
           out.width > 0.0f &&
           out.height > 0.0f;
}

bool CollisionBlocksMovement(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "block" ||
           lowered == "solid" ||
           lowered == "wall";
}

bool CollisionDisablesMovementBlock(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "none" ||
           lowered == "pass" ||
           lowered == "passable" ||
           lowered == "false" ||
           lowered == "off";
}

bool IsSupportedCollisionValue(const std::string& collision) {
    return CollisionBlocksMovement(collision) || CollisionDisablesMovementBlock(collision);
}

std::string XmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

bool XmlDecodeEntity(const std::string& entity, std::string& out) {
    if (entity == "amp") {
        out.push_back('&');
        return true;
    }
    if (entity == "lt") {
        out.push_back('<');
        return true;
    }
    if (entity == "gt") {
        out.push_back('>');
        return true;
    }
    if (entity == "quot") {
        out.push_back('"');
        return true;
    }
    if (entity == "apos") {
        out.push_back('\'');
        return true;
    }
    if (entity.size() > 1 && entity[0] == '#') {
        try {
            unsigned long codepoint = 0;
            if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                codepoint = std::stoul(entity.substr(2), nullptr, 16);
            } else {
                codepoint = std::stoul(entity.substr(1), nullptr, 10);
            }
            if (codepoint <= 0x7F) {
                out.push_back(static_cast<char>(codepoint));
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

bool XmlUnescape(const std::string& value, std::string& out) {
    out.clear();
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '&') {
            out.push_back(value[i]);
            continue;
        }
        const size_t end = value.find(';', i + 1);
        if (end == std::string::npos) {
            return false;
        }
        if (!XmlDecodeEntity(value.substr(i + 1, end - i - 1), out)) {
            return false;
        }
        i = end;
    }
    return true;
}

bool SkipXmlIgnorable(const std::string& text, size_t& pos, std::string& error) {
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size()) {
            return true;
        }
        if (text.compare(pos, 4, "<!--") == 0) {
            const size_t end = text.find("-->", pos + 4);
            if (end == std::string::npos) {
                error = "unterminated xml comment";
                return false;
            }
            pos = end + 3;
            continue;
        }
        if (text.compare(pos, 2, "<?") == 0) {
            const size_t end = text.find("?>", pos + 2);
            if (end == std::string::npos) {
                error = "unterminated xml declaration";
                return false;
            }
            pos = end + 2;
            continue;
        }
        if (text.compare(pos, 2, "<!") == 0) {
            const size_t end = text.find('>', pos + 2);
            if (end == std::string::npos) {
                error = "unterminated xml directive";
                return false;
            }
            pos = end + 1;
            continue;
        }
        break;
    }
    return true;
}

bool ParseXmlName(const std::string& text, size_t& pos, std::string& out) {
    if (pos >= text.size()) {
        return false;
    }
    const auto isNameStart = [](unsigned char ch) {
        return std::isalpha(ch) || ch == '_' || ch == ':';
    };
    const auto isNameChar = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == ':' || ch == '-' || ch == '.';
    };
    if (!isNameStart(static_cast<unsigned char>(text[pos]))) {
        return false;
    }
    const size_t start = pos++;
    while (pos < text.size() && isNameChar(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    out = text.substr(start, pos - start);
    return true;
}

bool ParseXmlQuotedValue(const std::string& text, size_t& pos, std::string& out, std::string& error) {
    if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
        error = "xml attribute missing quote";
        return false;
    }
    const char quote = text[pos++];
    const size_t start = pos;
    while (pos < text.size() && text[pos] != quote) {
        ++pos;
    }
    if (pos >= text.size()) {
        error = "unterminated xml attribute";
        return false;
    }
    if (!XmlUnescape(text.substr(start, pos - start), out)) {
        error = "invalid xml entity";
        return false;
    }
    ++pos;
    return true;
}

bool ParseXmlNode(const std::string& text, size_t& pos, XmlNode& node, std::string& error) {
    if (!SkipXmlIgnorable(text, pos, error)) {
        return false;
    }
    if (pos >= text.size() || text[pos] != '<') {
        error = "expected xml node";
        return false;
    }
    ++pos;
    if (pos < text.size() && text[pos] == '/') {
        error = "unexpected closing tag";
        return false;
    }

    if (!ParseXmlName(text, pos, node.name)) {
        error = "invalid xml node name";
        return false;
    }

    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size()) {
            error = "unexpected end of xml tag";
            return false;
        }
        if (text[pos] == '>') {
            ++pos;
            break;
        }
        if (text[pos] == '/' && pos + 1 < text.size() && text[pos + 1] == '>') {
            pos += 2;
            return true;
        }

        std::string key;
        if (!ParseXmlName(text, pos, key)) {
            error = "invalid xml attribute name";
            return false;
        }
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != '=') {
            error = "xml attribute missing =";
            return false;
        }
        ++pos;
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        std::string value;
        if (!ParseXmlQuotedValue(text, pos, value, error)) {
            return false;
        }
        node.attrs[key] = value;
    }

    while (true) {
        if (!SkipXmlIgnorable(text, pos, error)) {
            return false;
        }
        if (pos >= text.size()) {
            error = "unterminated xml node";
            return false;
        }
        if (text.compare(pos, 2, "</") == 0) {
            pos += 2;
            std::string closeName;
            if (!ParseXmlName(text, pos, closeName)) {
                error = "invalid xml closing tag";
                return false;
            }
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            if (pos >= text.size() || text[pos] != '>') {
                error = "invalid xml closing tag";
                return false;
            }
            ++pos;
            if (closeName != node.name) {
                error = "xml tag mismatch";
                return false;
            }
            return true;
        }
        if (text[pos] == '<') {
            XmlNode child;
            if (!ParseXmlNode(text, pos, child, error)) {
                return false;
            }
            node.children.push_back(std::move(child));
            continue;
        }

        const size_t nextTag = text.find('<', pos);
        const std::string_view rawText = nextTag == std::string::npos
            ? std::string_view(text).substr(pos)
            : std::string_view(text).substr(pos, nextTag - pos);
        const bool onlyWhitespace = std::all_of(rawText.begin(), rawText.end(), [](char ch) {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        });
        if (!onlyWhitespace) {
            error = "unsupported xml text node";
            return false;
        }
        pos = nextTag == std::string::npos ? text.size() : nextTag;
    }
}

bool ParseXmlDocument(const std::string& text, XmlNode& root, std::string& error) {
    size_t pos = 0;
    if (!SkipXmlIgnorable(text, pos, error)) {
        return false;
    }
    if (!ParseXmlNode(text, pos, root, error)) {
        return false;
    }
    if (!SkipXmlIgnorable(text, pos, error)) {
        return false;
    }
    if (pos != text.size()) {
        error = "unexpected trailing xml data";
        return false;
    }
    return true;
}

const XmlNode* FindFirstChild(const XmlNode& node, const std::string& name) {
    for (const XmlNode& child : node.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

std::vector<const XmlNode*> FindChildren(const XmlNode& node, const std::string& name) {
    std::vector<const XmlNode*> out;
    for (const XmlNode& child : node.children) {
        if (child.name == name) {
            out.push_back(&child);
        }
    }
    return out;
}

std::optional<std::string> AttrValue(const XmlNode& node, const std::string& key) {
    auto it = node.attrs.find(key);
    if (it == node.attrs.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool ReadProperties(const XmlNode* propertiesNode,
                    std::unordered_map<std::string, std::string>& out,
                    std::string& error) {
    out.clear();
    if (propertiesNode == nullptr) {
        return true;
    }
    for (const XmlNode* child : FindChildren(*propertiesNode, "property")) {
        const std::optional<std::string> name = AttrValue(*child, "name");
        if (!name.has_value() || name->empty()) {
            error = "tmx property missing name";
            return false;
        }
        const std::optional<std::string> value = AttrValue(*child, "value");
        out[*name] = value.value_or("");
    }
    return true;
}

Rect StampBounds(const Stamp& stamp, int tileSize) {
    const auto ref = ParseAtlasRef(stamp.asset);
    const float width = ref.has_value() ? std::max(1.0f, ref->source.width) : static_cast<float>(tileSize);
    const float height = ref.has_value() ? std::max(1.0f, ref->source.height) : static_cast<float>(tileSize);
    return Rect {
        static_cast<float>(stamp.x * tileSize) + ((static_cast<float>(tileSize) - width) * 0.5f),
        static_cast<float>((stamp.y + 1) * tileSize) - height,
        width,
        height
    };
}

bool StampGridFromObject(const XmlNode& object, int tileSize, const std::string& assetRef, int& outX, int& outY) {
    float x = 0.0f;
    float y = 0.0f;
    const std::optional<std::string> attrX = AttrValue(object, "x");
    const std::optional<std::string> attrY = AttrValue(object, "y");
    if (!attrX.has_value() || !attrY.has_value() ||
        !ParseFloatStrict(*attrX, x) ||
        !ParseFloatStrict(*attrY, y)) {
        return false;
    }

    const auto ref = ParseAtlasRef(assetRef);
    const float width = ref.has_value() ? std::max(1.0f, ref->source.width) : static_cast<float>(tileSize);
    const float height = ref.has_value() ? std::max(1.0f, ref->source.height) : static_cast<float>(tileSize);
    outX = static_cast<int>(std::lround((x - ((static_cast<float>(tileSize) - width) * 0.5f)) / static_cast<float>(tileSize)));
    outY = static_cast<int>(std::lround(((y + height) / static_cast<float>(tileSize)) - 1.0f));
    return true;
}

std::string FormatFloat(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text.empty()) {
        return "0";
    }
    return text;
}

void WriteIndent(std::ostringstream& out, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "    ";
    }
}

void WriteProperty(std::ostringstream& out, int depth, const std::string& name, const std::string& value) {
    WriteIndent(out, depth);
    out << "<property name=\"" << XmlEscape(name) << "\" value=\"" << XmlEscape(value) << "\"/>\n";
}

void WriteProperty(std::ostringstream& out, int depth, const std::string& name, int value) {
    WriteIndent(out, depth);
    out << "<property name=\"" << XmlEscape(name) << "\" type=\"int\" value=\"" << value << "\"/>\n";
}

void WriteProperty(std::ostringstream& out, int depth, const std::string& name, float value) {
    WriteIndent(out, depth);
    out << "<property name=\"" << XmlEscape(name) << "\" type=\"float\" value=\"" << XmlEscape(FormatFloat(value)) << "\"/>\n";
}

std::vector<std::pair<std::string, std::string>> SortedProperties(const std::unordered_map<std::string, std::string>& properties) {
    std::vector<std::pair<std::string, std::string>> sorted(properties.begin(), properties.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return sorted;
}

ParseResult ParseLegacyDocument(const std::string& text) {
    Document document;
    std::unordered_map<std::string, std::pair<AssetDomain, std::string>> assets;
    std::unordered_map<std::string, size_t> objectIndexById;
    bool hasFormat = false;
    std::istringstream stream(text);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "map_format") {
            int version = 0;
            if (!ParseIntStrict(value, version) || version != 2) {
                return Fail(lineNumber, "unsupported map_format");
            }
            hasFormat = true;
        } else if (key == "map_id") {
            document.mapId = value;
        } else if (key == "world_name") {
            document.worldName = value;
        } else if (key == "tile_size") {
            if (!ParseIntStrict(value, document.tileSize) || document.tileSize <= 0) {
                return Fail(lineNumber, "invalid tile size");
            }
        } else if (key == "size") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 2 ||
                !ParseIntStrict(parts[0], document.width) ||
                !ParseIntStrict(parts[1], document.height) ||
                document.width <= 0 ||
                document.height <= 0) {
                return Fail(lineNumber, "invalid map size");
            }
        } else if (key == "spawn") {
            if (!ParseVec2Strict(value, document.spawn)) {
                return Fail(lineNumber, "invalid spawn");
            }
        } else if (key == "property") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 2) {
                return Fail(lineNumber, "invalid property");
            }
            std::string propertyValue = parts[1];
            if (parts[0].rfind("player_sprite_", 0) == 0) {
                const auto resolved = ResolveAssetRef(propertyValue, AssetDomain::Character, assets);
                if (!resolved.has_value()) {
                    return Fail(lineNumber, "invalid player sprite");
                }
                propertyValue = *resolved;
            }
            document.properties[parts[0]] = propertyValue;
        } else if (key == "asset") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 3 || parts[0].empty() || parts[2].empty()) {
                return Fail(lineNumber, "invalid asset");
            }
            AssetDomain domain = AssetDomain::Map;
            if (parts[1] == "character") {
                domain = AssetDomain::Character;
            } else if (parts[1] != "map") {
                return Fail(lineNumber, "invalid asset domain");
            }
            assets[parts[0]] = std::make_pair(domain, parts[2]);
        } else if (key == "layer") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 2) {
                return Fail(lineNumber, "invalid layer");
            }
            Layer layer;
            layer.name = parts[0];
            layer.kind = parts[1];
            for (size_t i = 2; i < parts.size(); ++i) {
                const auto prop = SplitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) {
                    return Fail(lineNumber, "invalid layer property");
                }
                if (prop->first == "asset") {
                    const auto resolved = ResolveAssetRef(prop->second, AssetDomain::Map, assets);
                    if (!resolved.has_value()) {
                        return Fail(lineNumber, "invalid layer asset");
                    }
                    layer.asset = *resolved;
                    layer.kind = "image";
                } else if (prop->first == "tint") {
                    layer.tint = prop->second;
                } else if (prop->first == "parallax") {
                    if (!ParseFloatStrict(prop->second, layer.parallax)) {
                        return Fail(lineNumber, "invalid layer parallax");
                    }
                }
            }
            document.layers.push_back(std::move(layer));
        } else if (key == "tile") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 4) {
                return Fail(lineNumber, "invalid tile");
            }
            Stamp stamp;
            stamp.layer = parts[0];
            if (!ParseIntStrict(parts[1], stamp.x) || !ParseIntStrict(parts[2], stamp.y)) {
                return Fail(lineNumber, "invalid tile coordinates");
            }
            const auto resolved = ResolveAssetRef(parts[3], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid tile asset");
            }
            stamp.asset = *resolved;
            if (!AppendTile(document, stamp.layer, stamp.x, stamp.y, stamp.asset)) {
                return Fail(lineNumber, "invalid tile");
            }
        } else if (key == "line") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid line");
            }
            int x1 = 0;
            int y1 = 0;
            int x2 = 0;
            int y2 = 0;
            if (!ParseIntStrict(parts[1], x1) ||
                !ParseIntStrict(parts[2], y1) ||
                !ParseIntStrict(parts[3], x2) ||
                !ParseIntStrict(parts[4], y2)) {
                return Fail(lineNumber, "invalid line coordinates");
            }
            if (x1 != x2 && y1 != y2) {
                return Fail(lineNumber, "line must be horizontal or vertical");
            }
            const auto resolved = ResolveAssetRef(parts[5], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid line asset");
            }
            const int minX = std::min(x1, x2);
            const int maxX = std::max(x1, x2);
            const int minY = std::min(y1, y2);
            const int maxY = std::max(y1, y2);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    AppendTile(document, parts[0], x, y, *resolved);
                }
            }
        } else if (key == "fill") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid fill");
            }
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!ParseIntStrict(parts[1], x) ||
                !ParseIntStrict(parts[2], y) ||
                !ParseIntStrict(parts[3], width) ||
                !ParseIntStrict(parts[4], height) ||
                width <= 0 ||
                height <= 0) {
                return Fail(lineNumber, "invalid fill bounds");
            }
            const auto resolved = ResolveAssetRef(parts[5], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid fill asset");
            }
            for (int ty = y; ty < y + height; ++ty) {
                for (int tx = x; tx < x + width; ++tx) {
                    AppendTile(document, parts[0], tx, ty, *resolved);
                }
            }
        } else if (key == "tiles") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 3) {
                return Fail(lineNumber, "invalid tiles");
            }
            const auto resolved = ResolveAssetRef(parts[1], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid tiles asset");
            }
            for (size_t i = 2; i < parts.size(); ++i) {
                const std::vector<std::string> point = SplitEscaped(parts[i], ':');
                int x = 0;
                int y = 0;
                if (point.size() != 2 ||
                    !ParseIntStrict(point[0], x) ||
                    !ParseIntStrict(point[1], y)) {
                    return Fail(lineNumber, "invalid tiles coordinate");
                }
                AppendTile(document, parts[0], x, y, *resolved);
            }
        } else if (key == "entity") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid entity");
            }
            Object object;
            object.kind = parts[0];
            object.id = parts[1];
            if (!ParseFloatStrict(parts[2], object.bounds.x) ||
                !ParseFloatStrict(parts[3], object.bounds.y) ||
                !ParseFloatStrict(parts[4], object.bounds.width) ||
                !ParseFloatStrict(parts[5], object.bounds.height) ||
                object.bounds.width < 0.0f ||
                object.bounds.height < 0.0f) {
                return Fail(lineNumber, "invalid entity bounds");
            }
            if (objectIndexById.find(object.id) != objectIndexById.end()) {
                return Fail(lineNumber, "duplicate entity id");
            }
            objectIndexById[object.id] = document.objects.size();
            document.objects.push_back(std::move(object));
        } else if (key == "prop") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 3) {
                return Fail(lineNumber, "invalid entity property");
            }
            auto objectIt = objectIndexById.find(parts[0]);
            if (objectIt == objectIndexById.end()) {
                return Fail(lineNumber, "property references unknown entity");
            }
            std::string propValue = parts[2];
            if (parts[1] == "sprite") {
                const auto resolved = ResolveAssetRef(propValue, AssetDomain::Map, assets);
                if (!resolved.has_value()) {
                    return Fail(lineNumber, "invalid entity sprite");
                }
                propValue = *resolved;
            } else if (parts[1] == "collision" && !IsSupportedCollisionValue(propValue)) {
                return Fail(lineNumber, "invalid entity collision");
            }
            document.objects[objectIt->second].properties[parts[1]] = propValue;
        } else {
            return Fail(lineNumber, "unsupported map v2 key: " + key);
        }
    }

    if (!hasFormat) {
        return Fail(0, "missing map_format=2");
    }
    if (document.mapId.empty()) {
        return Fail(0, "missing map_id");
    }
    if (document.tileSize < 16 || document.tileSize > 128) {
        return Fail(0, "tile size out of supported range");
    }
    if (document.width < 8 || document.height < 8) {
        return Fail(0, "map dimensions out of supported range");
    }

    ParseResult result;
    result.document = std::move(document);
    result.ok = true;
    return result;
}

ParseResult ParseTmxDocument(const std::string& text) {
    XmlNode root;
    std::string xmlError;
    if (!ParseXmlDocument(text, root, xmlError)) {
        return Fail(0, "invalid tmx: " + xmlError);
    }
    if (root.name != "map") {
        return Fail(0, "invalid tmx root");
    }

    Document document;
    const std::optional<std::string> widthAttr = AttrValue(root, "width");
    const std::optional<std::string> heightAttr = AttrValue(root, "height");
    const std::optional<std::string> tileWidthAttr = AttrValue(root, "tilewidth");
    const std::optional<std::string> tileHeightAttr = AttrValue(root, "tileheight");
    if (!widthAttr.has_value() || !heightAttr.has_value() || !tileWidthAttr.has_value() || !tileHeightAttr.has_value()) {
        return Fail(0, "tmx missing map dimensions");
    }
    if (!ParseIntStrict(*widthAttr, document.width) ||
        !ParseIntStrict(*heightAttr, document.height) ||
        !ParseIntStrict(*tileWidthAttr, document.tileSize) ||
        document.width <= 0 ||
        document.height <= 0 ||
        document.tileSize <= 0) {
        return Fail(0, "invalid tmx map dimensions");
    }

    int tileHeight = 0;
    if (!ParseIntStrict(*tileHeightAttr, tileHeight) || tileHeight != document.tileSize) {
        return Fail(0, "tmx tileheight must match tilewidth");
    }

    std::unordered_map<std::string, std::string> mapProperties;
    if (!ReadProperties(FindFirstChild(root, "properties"), mapProperties, xmlError)) {
        return Fail(0, xmlError);
    }
    if (mapProperties["nganu_format"] != "tmx_v1") {
        return Fail(0, "unsupported tmx schema");
    }

    auto takeMapProperty = [&](const std::string& key) -> std::string {
        auto it = mapProperties.find(key);
        if (it == mapProperties.end()) {
            return {};
        }
        const std::string value = it->second;
        mapProperties.erase(it);
        return value;
    };

    document.mapId = takeMapProperty("map_id");
    document.worldName = takeMapProperty("world_name");
    const std::string spawnX = takeMapProperty("spawn_x");
    const std::string spawnY = takeMapProperty("spawn_y");
    if (!spawnX.empty() && !ParseFloatStrict(spawnX, document.spawn.x)) {
        return Fail(0, "invalid tmx spawn_x");
    }
    if (!spawnY.empty() && !ParseFloatStrict(spawnY, document.spawn.y)) {
        return Fail(0, "invalid tmx spawn_y");
    }
    if (spawnX.empty() || spawnY.empty()) {
        document.spawn = Vec2 {96.0f, 96.0f};
    }
    document.properties = std::move(mapProperties);

    for (const XmlNode* group : FindChildren(root, "objectgroup")) {
        std::unordered_map<std::string, std::string> groupProperties;
        if (!ReadProperties(FindFirstChild(*group, "properties"), groupProperties, xmlError)) {
            return Fail(0, xmlError);
        }

        const std::string role = groupProperties["nganu_role"];
        if (role == "map_layer") {
            Layer layer;
            layer.name = AttrValue(*group, "name").value_or("");
            layer.kind = groupProperties["kind"].empty() ? "tilemap" : groupProperties["kind"];
            layer.asset = groupProperties["asset"];
            layer.tint = groupProperties["tint"].empty() ? "#FFFFFFFF" : groupProperties["tint"];
            if (!groupProperties["parallax"].empty() &&
                !ParseFloatStrict(groupProperties["parallax"], layer.parallax)) {
                return Fail(0, "invalid tmx layer parallax");
            }
            document.layers.push_back(layer);

            for (const XmlNode* objectNode : FindChildren(*group, "object")) {
                std::unordered_map<std::string, std::string> objectProperties;
                if (!ReadProperties(FindFirstChild(*objectNode, "properties"), objectProperties, xmlError)) {
                    return Fail(0, xmlError);
                }
                const std::string assetRef = objectProperties["asset_ref"];
                if (assetRef.empty()) {
                    return Fail(0, "tmx stamp missing asset_ref");
                }
                Stamp stamp;
                stamp.layer = layer.name;
                stamp.asset = assetRef;
                if (!StampGridFromObject(*objectNode, document.tileSize, stamp.asset, stamp.x, stamp.y)) {
                    const std::string gridX = objectProperties["grid_x"];
                    const std::string gridY = objectProperties["grid_y"];
                    if (!ParseIntStrict(gridX, stamp.x) || !ParseIntStrict(gridY, stamp.y)) {
                        return Fail(0, "tmx stamp missing grid position");
                    }
                }
                document.stamps.push_back(std::move(stamp));
            }
        } else if (role == "map_objects") {
            std::unordered_map<std::string, size_t> objectIndexById;
            for (const XmlNode* objectNode : FindChildren(*group, "object")) {
                Object object;
                object.id = AttrValue(*objectNode, "name").value_or("");
                object.kind = AttrValue(*objectNode, "type").value_or(AttrValue(*objectNode, "class").value_or(""));
                const std::optional<std::string> attrX = AttrValue(*objectNode, "x");
                const std::optional<std::string> attrY = AttrValue(*objectNode, "y");
                const std::optional<std::string> attrW = AttrValue(*objectNode, "width");
                const std::optional<std::string> attrH = AttrValue(*objectNode, "height");
                if (object.id.empty() || object.kind.empty() ||
                    !attrX.has_value() || !attrY.has_value() || !attrW.has_value() || !attrH.has_value() ||
                    !ParseFloatStrict(*attrX, object.bounds.x) ||
                    !ParseFloatStrict(*attrY, object.bounds.y) ||
                    !ParseFloatStrict(*attrW, object.bounds.width) ||
                    !ParseFloatStrict(*attrH, object.bounds.height) ||
                    object.bounds.width < 0.0f ||
                    object.bounds.height < 0.0f) {
                    return Fail(0, "invalid tmx object");
                }
                if (objectIndexById.find(object.id) != objectIndexById.end()) {
                    return Fail(0, "duplicate tmx object id");
                }
                if (!ReadProperties(FindFirstChild(*objectNode, "properties"), object.properties, xmlError)) {
                    return Fail(0, xmlError);
                }
                if (object.properties.count("collision") != 0 &&
                    !IsSupportedCollisionValue(object.properties["collision"])) {
                    return Fail(0, "invalid tmx object collision");
                }
                objectIndexById[object.id] = document.objects.size();
                document.objects.push_back(std::move(object));
            }
        }
    }

    if (document.mapId.empty()) {
        return Fail(0, "missing map_id");
    }
    if (document.tileSize < 16 || document.tileSize > 128) {
        return Fail(0, "tile size out of supported range");
    }
    if (document.width < 8 || document.height < 8) {
        return Fail(0, "map dimensions out of supported range");
    }

    ParseResult result;
    result.document = std::move(document);
    result.ok = true;
    return result;
}

}

std::vector<std::string> SplitEscaped(const std::string& value, char delim) {
    std::vector<std::string> parts;
    std::string current;
    bool escaping = false;
    for (char ch : value) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == delim) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(current);
    return parts;
}

std::optional<std::pair<std::string, std::string>> SplitPropertyAssignment(const std::string& value, char delim) {
    std::string key;
    std::string remainder;
    bool escaping = false;
    bool foundDelim = false;
    for (char ch : value) {
        if (escaping) {
            if (foundDelim) {
                remainder.push_back(ch);
            } else {
                key.push_back(ch);
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (!foundDelim && ch == delim) {
            foundDelim = true;
            continue;
        }
        if (foundDelim) {
            remainder.push_back(ch);
        } else {
            key.push_back(ch);
        }
    }
    if (!foundDelim) {
        return std::nullopt;
    }
    return std::make_pair(key, remainder);
}

std::string EscapeValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == ',' || ch == ':') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool ParseFloatStrict(const std::string& value, float& out) {
    try {
        size_t consumed = 0;
        out = std::stof(value, &consumed);
        return consumed == value.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

bool ParseIntStrict(const std::string& value, int& out) {
    try {
        size_t consumed = 0;
        out = std::stoi(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool ParseRectStrict(const std::string& value, Rect& out) {
    const std::vector<std::string> parts = SplitEscaped(value, ',');
    if (parts.size() != 4) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) &&
           ParseFloatStrict(parts[1], out.y) &&
           ParseFloatStrict(parts[2], out.width) &&
           ParseFloatStrict(parts[3], out.height);
}

bool ParseVec2Strict(const std::string& value, Vec2& out) {
    const std::vector<std::string> parts = SplitEscaped(value, ',');
    if (parts.size() != 2) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) && ParseFloatStrict(parts[1], out.y);
}

ParseResult ParseDocument(const std::string& text) {
    const std::string trimmed = TrimCopy(text);
    if (!trimmed.empty() && trimmed.front() == '<') {
        return ParseTmxDocument(text);
    }
    return ParseLegacyDocument(text);
}

std::string SerializeDocumentAsTmx(const Document& document, const TmxWriteOptions& /*options*/) {
    int nextLayerId = 1;
    int nextObjectId = 1;
    nextLayerId += static_cast<int>(document.layers.size());
    nextLayerId += 1;
    nextObjectId += static_cast<int>(document.stamps.size() + document.objects.size());

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<map version=\"1.10\" tiledversion=\"1.11.0\" orientation=\"orthogonal\" renderorder=\"right-down\""
        << " width=\"" << document.width << "\""
        << " height=\"" << document.height << "\""
        << " tilewidth=\"" << document.tileSize << "\""
        << " tileheight=\"" << document.tileSize << "\""
        << " infinite=\"0\""
        << " nextlayerid=\"" << nextLayerId << "\""
        << " nextobjectid=\"" << nextObjectId << "\">\n";

    WriteIndent(out, 1);
    out << "<properties>\n";
    WriteProperty(out, 2, "nganu_format", "tmx_v1");
    WriteProperty(out, 2, "map_id", document.mapId);
    WriteProperty(out, 2, "world_name", document.worldName);
    WriteProperty(out, 2, "spawn_x", document.spawn.x);
    WriteProperty(out, 2, "spawn_y", document.spawn.y);
    for (const auto& [key, value] : SortedProperties(document.properties)) {
        WriteProperty(out, 2, key, value);
    }
    WriteIndent(out, 1);
    out << "</properties>\n";

    int layerId = 1;
    int objectId = 1;
    for (const Layer& layer : document.layers) {
        WriteIndent(out, 1);
        out << "<objectgroup id=\"" << layerId++ << "\" name=\"" << XmlEscape(layer.name) << "\">\n";
        WriteIndent(out, 2);
        out << "<properties>\n";
        WriteProperty(out, 3, "nganu_role", "map_layer");
        WriteProperty(out, 3, "kind", layer.kind.empty() ? "tilemap" : layer.kind);
        if (!layer.asset.empty()) {
            WriteProperty(out, 3, "asset", layer.asset);
        }
        WriteProperty(out, 3, "tint", layer.tint.empty() ? "#FFFFFFFF" : layer.tint);
        WriteProperty(out, 3, "parallax", layer.parallax);
        WriteIndent(out, 2);
        out << "</properties>\n";

        for (const Stamp& stamp : document.stamps) {
            if (stamp.layer != layer.name) {
                continue;
            }
            const Rect bounds = StampBounds(stamp, document.tileSize);
            WriteIndent(out, 2);
            out << "<object id=\"" << objectId++ << "\""
                << " name=\"stamp\""
                << " type=\"stamp\""
                << " x=\"" << XmlEscape(FormatFloat(bounds.x)) << "\""
                << " y=\"" << XmlEscape(FormatFloat(bounds.y)) << "\""
                << " width=\"" << XmlEscape(FormatFloat(bounds.width)) << "\""
                << " height=\"" << XmlEscape(FormatFloat(bounds.height)) << "\">\n";
            WriteIndent(out, 3);
            out << "<properties>\n";
            WriteProperty(out, 4, "asset_ref", stamp.asset);
            WriteProperty(out, 4, "grid_x", stamp.x);
            WriteProperty(out, 4, "grid_y", stamp.y);
            WriteIndent(out, 3);
            out << "</properties>\n";
            WriteIndent(out, 2);
            out << "</object>\n";
        }

        WriteIndent(out, 1);
        out << "</objectgroup>\n";
    }

    WriteIndent(out, 1);
    out << "<objectgroup id=\"" << layerId << "\" name=\"objects\">\n";
    WriteIndent(out, 2);
    out << "<properties>\n";
    WriteProperty(out, 3, "nganu_role", "map_objects");
    WriteIndent(out, 2);
    out << "</properties>\n";
    for (const Object& object : document.objects) {
        WriteIndent(out, 2);
        out << "<object id=\"" << objectId++ << "\""
            << " name=\"" << XmlEscape(object.id) << "\""
            << " type=\"" << XmlEscape(object.kind) << "\""
            << " x=\"" << XmlEscape(FormatFloat(object.bounds.x)) << "\""
            << " y=\"" << XmlEscape(FormatFloat(object.bounds.y)) << "\""
            << " width=\"" << XmlEscape(FormatFloat(object.bounds.width)) << "\""
            << " height=\"" << XmlEscape(FormatFloat(object.bounds.height)) << "\">\n";
        if (!object.properties.empty()) {
            WriteIndent(out, 3);
            out << "<properties>\n";
            for (const auto& [key, value] : SortedProperties(object.properties)) {
                WriteProperty(out, 4, key, value);
            }
            WriteIndent(out, 3);
            out << "</properties>\n";
        }
        WriteIndent(out, 2);
        out << "</object>\n";
    }
    WriteIndent(out, 1);
    out << "</objectgroup>\n";
    out << "</map>\n";
    return out.str();
}

std::optional<AtlasRef> ParseAtlasRef(const std::string& asset) {
    const std::vector<std::string> parts = SplitEscaped(asset, '@');
    if (parts.size() != 5) {
        return std::nullopt;
    }

    AtlasRef ref;
    ref.file = parts[0];
    const size_t domainSep = ref.file.find(':');
    if (domainSep != std::string::npos) {
        const std::string domain = ref.file.substr(0, domainSep);
        ref.file = ref.file.substr(domainSep + 1);
        ref.domain = (domain == "character") ? AssetDomain::Character : AssetDomain::Map;
    }

    if (!ParseFloatStrict(parts[1], ref.source.x) ||
        !ParseFloatStrict(parts[2], ref.source.y) ||
        !ParseFloatStrict(parts[3], ref.source.width) ||
        !ParseFloatStrict(parts[4], ref.source.height)) {
        return std::nullopt;
    }

    ref.valid = !ref.file.empty() && ref.source.width > 0.0f && ref.source.height > 0.0f;
    if (!ref.valid) {
        return std::nullopt;
    }
    return ref;
}

std::string AssetFileName(const std::string& ref) {
    if (const std::optional<AtlasRef> atlasRef = ParseAtlasRef(ref)) {
        return atlasRef->file;
    }
    std::string head = ref;
    const size_t domainSep = head.find(':');
    if (domainSep != std::string::npos) {
        head = head.substr(domainSep + 1);
    }
    return head;
}

AssetDomain AssetDomainForRef(const std::string& ref, AssetDomain fallbackDomain) {
    if (const std::optional<AtlasRef> atlasRef = ParseAtlasRef(ref)) {
        return atlasRef->domain;
    }
    const size_t domainSep = ref.find(':');
    if (domainSep == std::string::npos) {
        return fallbackDomain;
    }
    return ref.substr(0, domainSep) == "character" ? AssetDomain::Character : AssetDomain::Map;
}

std::string AtlasMetaKey(const std::string& file, int x, int y, int w, int h) {
    return file + "@" + std::to_string(x) + "@" + std::to_string(y) + "@" + std::to_string(w) + "@" + std::to_string(h);
}

std::unordered_map<std::string, AtlasTileMeta> ParseAtlasMetadata(const std::string& text, const std::string& fileName) {
    std::unordered_map<std::string, AtlasTileMeta> metadata;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t sep = line.find('=');
        if (sep == std::string::npos || line.substr(0, sep) != "tile") {
            continue;
        }

        const std::vector<std::string> parts = SplitEscaped(line.substr(sep + 1), ',');
        if (parts.size() < 4) {
            continue;
        }

        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if (!ParseIntStrict(parts[0], x) ||
            !ParseIntStrict(parts[1], y) ||
            !ParseIntStrict(parts[2], w) ||
            !ParseIntStrict(parts[3], h) ||
            w <= 0 ||
            h <= 0) {
            continue;
        }

        AtlasTileMeta meta;
        for (size_t i = 4; i < parts.size(); ++i) {
            const auto prop = SplitPropertyAssignment(parts[i], ':');
            if (!prop.has_value()) {
                continue;
            }
            if (prop->first == "collision") {
                meta.collision = ToLower(prop->second);
                meta.blocksMovement = CollisionBlocksMovement(meta.collision);
            } else if (prop->first == "tag") {
                meta.tag = prop->second;
            } else if (prop->first == "collider" || prop->first == "hitbox") {
                Rect collider;
                if (ParseLocalRect(prop->second, collider)) {
                    meta.collider = collider;
                    meta.hasCollider = true;
                }
            }
        }

        metadata[AtlasMetaKey(fileName, x, y, w, h)] = std::move(meta);
    }
    return metadata;
}

void AddUniqueAssetRef(std::vector<std::string>& refs, const std::string& file) {
    if (file.empty()) {
        return;
    }
    if (std::find(refs.begin(), refs.end(), file) == refs.end()) {
        refs.push_back(file);
    }
}

void CollectReferencedAssets(const Document& document,
                             std::vector<std::string>& mapImageRefs,
                             std::vector<std::string>& characterImageRefs) {
    mapImageRefs.clear();
    characterImageRefs.clear();
    for (const auto& [key, value] : document.properties) {
        if (key.rfind("player_sprite_", 0) == 0) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, value, AssetDomain::Character);
        }
    }
    for (const Layer& layer : document.layers) {
        if (layer.kind == "image" && !layer.asset.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, layer.asset, AssetDomain::Map);
        }
    }
    for (const Object& object : document.objects) {
        auto spriteIt = object.properties.find("sprite");
        if (spriteIt != object.properties.end() && !spriteIt->second.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, spriteIt->second, AssetDomain::Map);
        }
    }
    for (const Stamp& stamp : document.stamps) {
        if (!stamp.asset.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, stamp.asset, AssetDomain::Map);
        }
    }
}

}
}
