#pragma once
#include "activity_table.h"

namespace sunrise::middleware::content::packages::tables::activities {
struct StringRef {
    std::uint32_t bank{0xFFFFU}, hash{0x811C9DC5U};
};
struct DisplayRefs {
    StringRef title{}, description{}, type{}, expansion{};
    std::uint16_t category{0xFFFF};
    std::uint32_t style{0x811C9DC5};
};
/** Client slot 3 uses the exact same ordinal AND authored identity as public slot 4. */
[[nodiscard]] inline bool
display_refs(std::span<const std::byte> display,
             std::span<const std::byte> types,
             std::span<const state::build_data::activities::Definition> rows,
             std::span<DisplayRefs> output) noexcept {
    Array a{}, t{};
    if (!find_array_at(display, 8, a) || a.count != rows.size() || output.size() < rows.size()
        || !find_array_at(types, 8, t) || t.count > 256 || a.dataOffset > display.size()
        || a.count > (display.size() - a.dataOffset) / 16 || t.dataOffset > types.size()
        || t.count > (types.size() - t.dataOffset) / 128) {
        return false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto entry = a.dataOffset + i * 16;
        std::uint32_t hash{};
        std::size_t record{}, d{};
        output[i] = {};
        if (!read(display, entry, hash) || hash != rows[i].hash
            || !relative(display, entry + 8, record) || !relative(display, record, d)
            || !read(display, d + 4, output[i].title)
            || !read(display, d + 12, output[i].description)
            || !read(display, d + 20, output[i].expansion)
            || !read(display, record + 8, output[i].category)
            || !read(display, d + 32, output[i].style)) {
            return false;
        }
        if (rows[i].nativeType < t.count
            && !read(types, t.dataOffset + rows[i].nativeType * 128 + 4, output[i].type)) {
            return false;
        }
    }
    return true;
}
/** Decode one bank-scoped English string, with bounded combinations, parts and UTF-8 shifting.
 * Never resolves a hash against another bank; oversize/malformed text is unavailable, not
 * truncated. */
[[nodiscard]] inline bool localized_string(std::span<const std::byte> container,
                                           std::span<const std::byte> language,
                                           std::uint32_t hash,
                                           std::span<char> output) noexcept {
    if (output.empty()) {
        return false;
    }
    output[0] = '\0';
    Array hashes{}, combinations{};
    if (hash == 0x811C9DC5U || !find_array_at(container, 8, hashes)
        || !find_array_at(language, 72, combinations) || hashes.count != combinations.count
        || hashes.dataOffset > container.size()
        || hashes.count > (container.size() - hashes.dataOffset) / 4
        || combinations.dataOffset > language.size()
        || combinations.count > (language.size() - combinations.dataOffset) / 16) {
        return false;
    }
    std::size_t ordinal = static_cast<std::size_t>(hashes.count);
    for (std::size_t i = 0; i < hashes.count; ++i) {
        std::uint32_t value{};
        if (read(container, hashes.dataOffset + i * 4, value) && value == hash) {
            ordinal = i;
            break;
        }
    }
    if (ordinal == hashes.count) {
        return false;
    }
    const auto combination = combinations.dataOffset + ordinal * 16;
    std::int64_t parts{};
    std::size_t first{}, written{};
    if (!read(language, combination + 8, parts) || parts < 0 || parts > 32) {
        return false;
    }
    if (parts == 0) {
        return true;
    }
    if (!relative(language, combination, first)
        || static_cast<std::uint64_t>(parts) > (language.size() - first) / 32) {
        return false;
    }
    for (std::size_t p = 0; p < static_cast<std::size_t>(parts); ++p) {
        const auto part = first + p * 32;
        std::size_t data{};
        std::uint16_t bytes{}, shift{};
        if (!relative(language, part + 8, data) || !read(language, part + 20, bytes)
            || !read(language, part + 24, shift) || bytes > language.size() - data
            || bytes >= output.size() - written) {
            output[0] = '\0';
            return false;
        }
        for (std::size_t i = 0; i < bytes;) {
            const auto c = static_cast<std::uint8_t>(language[data + i]);
            const std::size_t width = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            if (width > bytes - i || c >= 0xF5) {
                output[0] = '\0';
                return false;
            }
            for (std::size_t j = 0; j < width; ++j) {
                auto decoded = static_cast<std::uint8_t>(language[data + i + j]);
                if (j + 1 == width) {
                    decoded = static_cast<std::uint8_t>(decoded + shift);
                }
                output[written++] = static_cast<char>(decoded);
            }
            i += width;
        }
    }
    output[written] = '\0';
    return true;
}
} // namespace sunrise::middleware::content::packages::tables::activities
