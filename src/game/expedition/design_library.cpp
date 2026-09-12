#include "game/expedition/design_library.hpp"
#include "core/sha256.hpp"
#include <json.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace voxy::game::expedition {
namespace {
constexpr std::string_view digits = "0123456789abcdef";
bool whitespace(uint32_t c) noexcept {
    return c == 0x20 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
        || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}
uint64_t integer(std::span<const std::byte> bytes, size_t offset, size_t count) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint64_t(std::to_integer<uint8_t>(bytes[offset + i])) << (8 * i);
    return value;
}
void append(std::vector<std::byte>& bytes, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 255));
}
bool validRecord(const SavedDesign& row) noexcept {
    return row.id != 0 && row.revision != 0 && validDesignName(row.name)
        && validDesignBlueprintEnvelope(row.blueprint);
}
bool validImage(const DesignLibraryImage& image) noexcept {
    if (image.nextId == 0 || image.rows.size() > kMaximumLibraryDesigns) return false;
    uint64_t previous = 0;
    for (size_t i = 0; i < image.rows.size(); ++i) {
        const auto& row = image.rows[i];
        if (!validRecord(row.current) || row.current.id <= previous || row.current.id >= image.nextId) return false;
        previous = row.current.id;
        if (row.backup && (!validRecord(*row.backup) || row.backup->id != row.current.id
            || row.backup->revision >= row.current.revision)) return false;
        for (size_t j = 0; j < i; ++j) if (image.rows[j].current.name == row.current.name) return false;
    }
    return true;
}
void appendRecord(std::vector<std::byte>& bytes, const SavedDesign& row) {
    append(bytes, row.id, 8); append(bytes, row.revision, 8);
    append(bytes, row.name.size(), 4); append(bytes, row.blueprint.size(), 4);
    const auto name = std::as_bytes(std::span(row.name));
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.insert(bytes.end(), row.blueprint.begin(), row.blueprint.end());
}
} // namespace

bool validDesignName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaximumDesignNameBytes) return false;
    uint32_t first = 0, last = 0;
    for (size_t i = 0; i < name.size();) {
        const size_t begin = i;
        const auto initial = static_cast<uint8_t>(name[i++]);
        uint32_t code = initial;
        size_t trailing = 0;
        uint32_t minimum = 0;
        if (initial >= 0xc2 && initial <= 0xdf) { trailing = 1; code = initial & 0x1fu; minimum = 0x80; }
        else if (initial >= 0xe0 && initial <= 0xef) { trailing = 2; code = initial & 0x0fu; minimum = 0x800; }
        else if (initial >= 0xf0 && initial <= 0xf4) { trailing = 3; code = initial & 7u; minimum = 0x10000; }
        else if (initial >= 0x80) return false;
        if (trailing > name.size() - i) return false;
        for (size_t n = 0; n < trailing; ++n) {
            const auto c = static_cast<uint8_t>(name[i++]);
            if ((c & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (c & 0x3fu);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)
            || code < 0x20 || code == 0x7f) return false;
        if (begin == 0) first = code;
        last = code;
    }
    return !whitespace(first) && !whitespace(last);
}
bool validDesignBlueprintEnvelope(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 48 || bytes.size() > kMaximumDesignBlueprintBytes) return false;
    constexpr std::array magic{std::byte{'S'}, std::byte{'V'}, std::byte{'B'}, std::byte{'P'}};
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()) || integer(bytes, 4, 4) != 1) return false;
    const auto parts = integer(bytes, 8, 4), connections = integer(bytes, 12, 4);
    if (parts == 0 || parts > 256 || connections > 1024 || bytes.size() != 48 + parts * 59 + connections * 70) return false;
    const auto hash = core::sha256(bytes.first(bytes.size() - 32));
    return std::equal(hash.bytes.begin(), hash.bytes.end(), bytes.end() - 32);
}
std::string designBlueprintHex(std::span<const std::byte> bytes) {
    std::string text; text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        const auto value = std::to_integer<uint8_t>(byte);
        text.push_back(digits[value >> 4]); text.push_back(digits[value & 15]);
    }
    return text;
}
bool parseDesignBlueprintHex(std::string_view text, std::vector<std::byte>& output) {
    if (text.size() < 96 || text.size() > 2 * kMaximumDesignBlueprintBytes || text.size() % 2) return false;
    std::vector<std::byte> bytes; bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const auto a = digits.find(text[i]), b = digits.find(text[i + 1]);
        if (a == digits.npos || b == digits.npos) return false;
        bytes.push_back(static_cast<std::byte>(a * 16 + b));
    }
    if (!validDesignBlueprintEnvelope(bytes)) return false;
    output = std::move(bytes); return true;
}
bool parseDesignFile(std::string_view text, DesignFile& output, std::string& error) {
    error = "This design file is invalid or incompatible.";
    if (text.size() > kMaximumDesignFileBytes) { error = "This design file is too large."; return false; }
    try {
        const auto object = nlohmann::json::parse(text);
        if (!object.is_object() || object.size() != 3 || !object.contains("version") || object["version"] != 1
            || !object.contains("name") || !object["name"].is_string()
            || !object.contains("blueprint") || !object["blueprint"].is_string()) return false;
        DesignFile value{object["name"].get<std::string>(), {}};
        if (!validDesignName(value.name) || !parseDesignBlueprintHex(object["blueprint"].get_ref<const std::string&>(), value.blueprint)) return false;
        output = std::move(value); error.clear(); return true;
    } catch (const std::bad_alloc&) { error = "Design memory capacity was reached."; return false; }
    catch (const nlohmann::json::exception&) { return false; }
}
bool encodeDesignFile(const DesignFile& value, std::string& output, std::string& error) {
    error = "This design cannot be exported.";
    if (!validDesignName(value.name) || !validDesignBlueprintEnvelope(value.blueprint)) return false;
    try {
        auto text = nlohmann::json{{"version", 1}, {"name", value.name}, {"blueprint", designBlueprintHex(value.blueprint)}}.dump();
        if (text.size() > kMaximumDesignFileBytes) return false;
        output = std::move(text); error.clear(); return true;
    } catch (const std::bad_alloc&) { error = "Design memory capacity was reached."; return false; }
    catch (const nlohmann::json::exception&) { return false; }
}
bool encodeDesignLibrary(const DesignLibraryImage& image, std::vector<std::byte>& output, std::string& error) {
    error = "The saved design library is invalid.";
    if (!validImage(image)) return false;
    try {
        std::vector<std::byte> bytes;
        for (char c : std::string_view("SVBL")) bytes.push_back(static_cast<std::byte>(c));
        append(bytes, 1, 4); append(bytes, image.nextId, 8); append(bytes, image.rows.size(), 4);
        for (const auto& row : image.rows) {
            appendRecord(bytes, row.current); append(bytes, row.backup ? 1 : 0, 1);
            if (row.backup) appendRecord(bytes, *row.backup);
        }
        const auto hash = core::sha256(bytes); bytes.insert(bytes.end(), hash.bytes.begin(), hash.bytes.end());
        if (bytes.size() > kMaximumDesignLibraryBytes) return false;
        output = std::move(bytes); error.clear(); return true;
    } catch (const std::bad_alloc&) { error = "Design memory capacity was reached."; return false; }
}
bool decodeDesignLibrary(std::span<const std::byte> bytes, DesignLibraryImage& output, std::string& error) {
    error = "The saved design library is damaged or incompatible.";
    if (bytes.size() < 52 || bytes.size() > kMaximumDesignLibraryBytes) return false;
    constexpr std::array magic{std::byte{'S'}, std::byte{'V'}, std::byte{'B'}, std::byte{'L'}};
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()) || integer(bytes, 4, 4) != 1) return false;
    const auto count = integer(bytes, 16, 4);
    if (count > kMaximumLibraryDesigns) return false;
    const auto hash = core::sha256(bytes.first(bytes.size() - 32));
    if (!std::equal(hash.bytes.begin(), hash.bytes.end(), bytes.end() - 32)) return false;
    try {
        DesignLibraryImage image; image.nextId = integer(bytes, 8, 8); image.rows.reserve(static_cast<size_t>(count));
        size_t offset = 20;
        const size_t end = bytes.size() - 32;
        const auto record = [&](SavedDesign& value) {
            if (end - offset < 24) return false;
            value.id = integer(bytes, offset, 8); value.revision = integer(bytes, offset + 8, 8);
            const auto nameSize = integer(bytes, offset + 16, 4), blueprintSize = integer(bytes, offset + 20, 4); offset += 24;
            if (nameSize == 0 || nameSize > kMaximumDesignNameBytes || blueprintSize > kMaximumDesignBlueprintBytes
                || nameSize + blueprintSize > end - offset) return false;
            value.name.assign(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<size_t>(nameSize)); offset += static_cast<size_t>(nameSize);
            const auto payload = bytes.subspan(offset, static_cast<size_t>(blueprintSize));
            value.blueprint.assign(payload.begin(), payload.end()); offset += static_cast<size_t>(blueprintSize); return true;
        };
        for (uint64_t i = 0; i < count; ++i) {
            SavedDesignRow row;
            if (!record(row.current) || offset == end) return false;
            const auto backup = std::to_integer<uint8_t>(bytes[offset++]);
            if (backup > 1) return false;
            if (backup) { row.backup.emplace(); if (!record(*row.backup)) return false; }
            image.rows.push_back(std::move(row));
        }
        if (offset != end || !validImage(image)) return false;
        output = std::move(image); error.clear(); return true;
    } catch (const std::bad_alloc&) { error = "Design memory capacity was reached."; return false; }
}
} // namespace voxy::game::expedition
