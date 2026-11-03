#include "app/launch/ServerList.h"
#include "app/instances/Instances.h"
#include "app/launch/GameOptions.h"
#include "app/system/AsyncTasks.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>

namespace ServerList {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;
        using Clock = std::chrono::steady_clock;

        constexpr auto RESCAN = std::chrono::seconds(10);
        constexpr int MAX_DEPTH = 64;

        enum Tag : uint8_t {
            END = 0,
            BYTE = 1,
            SHORT = 2,
            INT = 3,
            LONG = 4,
            FLOAT = 5,
            DOUBLE = 6,
            BYTE_ARRAY = 7,
            STRING = 8,
            LIST = 9,
            COMPOUND = 10,
            INT_ARRAY = 11,
            LONG_ARRAY = 12,
        };

        struct Found {
            std::string name;
            std::string address;
            std::string icon;
            bool hidden = false;
        };

        AsyncTasks tasks;
        std::vector<Server> list;
        unsigned listRevision = 0;
        bool scanning = false;
        bool scanned = false;
        Clock::time_point scannedAt;

        void append(std::string& out, uint32_t code) {
            if (code < 0x80) {
                out += (char) code;
            } else if (code < 0x800) {
                out += (char) (0xC0 | (code >> 6));
                out += (char) (0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += (char) (0xE0 | (code >> 12));
                out += (char) (0x80 | ((code >> 6) & 0x3F));
                out += (char) (0x80 | (code & 0x3F));
            } else {
                out += (char) (0xF0 | (code >> 18));
                out += (char) (0x80 | ((code >> 12) & 0x3F));
                out += (char) (0x80 | ((code >> 6) & 0x3F));
                out += (char) (0x80 | (code & 0x3F));
            }
        }

        std::string standardUtf8(const unsigned char* bytes, size_t size) {
            std::string out;
            out.reserve(size);
            auto unit = [bytes](size_t at) {
                return (uint32_t) ((bytes[at] & 0x0F) << 12 | (bytes[at + 1] & 0x3F) << 6 | (bytes[at + 2] & 0x3F));
            };
            for (size_t i = 0; i < size;) {
                if (bytes[i] == 0xC0 && i + 1 < size && bytes[i + 1] == 0x80) {
                    i += 2;
                    continue;
                }
                if (bytes[i] == 0xED && i + 6 <= size && bytes[i + 3] == 0xED) {
                    uint32_t high = unit(i);
                    uint32_t low = unit(i + 3);
                    if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF) {
                        append(out, 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00));
                        i += 6;
                        continue;
                    }
                }
                out += (char) bytes[i];
                i++;
            }
            return out;
        }

        class Reader {
        public:
            explicit Reader(const std::string& bytes) : data((const unsigned char*) bytes.data()), size(bytes.size()) {}

            bool bad = false;

            uint8_t u8() {
                return need(1) ? data[at++] : 0;
            }

            uint16_t u16() {
                if (!need(2)) {
                    return 0;
                }
                uint16_t value = (uint16_t) (data[at] << 8 | data[at + 1]);
                at += 2;
                return value;
            }

            int32_t i32() {
                if (!need(4)) {
                    return 0;
                }
                uint32_t value = (uint32_t) data[at] << 24 | (uint32_t) data[at + 1] << 16 |
                        (uint32_t) data[at + 2] << 8 | (uint32_t) data[at + 3];
                at += 4;
                return (int32_t) value;
            }

            std::string text() {
                uint16_t length = u16();
                if (!need(length)) {
                    return std::string();
                }
                std::string value = standardUtf8(data + at, length);
                at += length;
                return value;
            }

            void skip(uint8_t type, int depth) {
                if (bad || depth > MAX_DEPTH) {
                    bad = true;
                    return;
                }
                switch (type) {
                    case BYTE: advance(1); break;
                    case SHORT: advance(2); break;
                    case INT:
                    case FLOAT: advance(4); break;
                    case LONG:
                    case DOUBLE: advance(8); break;
                    case BYTE_ARRAY: advance(count(), 1); break;
                    case INT_ARRAY: advance(count(), 4); break;
                    case LONG_ARRAY: advance(count(), 8); break;
                    case STRING: advance(u16()); break;
                    case LIST: {
                        uint8_t inner = u8();
                        int32_t items = count();
                        for (int32_t i = 0; i < items && !bad; i++) {
                            skip(inner, depth + 1);
                        }
                        break;
                    }
                    case COMPOUND:
                        for (uint8_t inner = u8(); inner != END && !bad; inner = u8()) {
                            advance(u16());
                            skip(inner, depth + 1);
                        }
                        break;
                    default: bad = true; break;
                }
            }

            int32_t count() {
                int32_t value = i32();
                if (value < 0) {
                    bad = true;
                    return 0;
                }
                return value;
            }

        private:
            const unsigned char* data;
            size_t size;
            size_t at = 0;

            bool need(size_t bytes) {
                if (bad || size - at < bytes) {
                    bad = true;
                    return false;
                }
                return true;
            }

            void advance(size_t items, size_t width = 1) {
                if (width != 0 && items > (size - at) / width) {
                    bad = true;
                    return;
                }
                if (need(items * width)) {
                    at += items * width;
                }
            }
        };

        Found entry(Reader& reader) {
            Found found;
            for (uint8_t type = reader.u8(); type != END && !reader.bad; type = reader.u8()) {
                std::string key = reader.text();
                if (type == STRING && (key == "name" || key == "ip" || key == "icon")) {
                    std::string value = reader.text();
                    (key == "name" ? found.name : key == "ip" ? found.address : found.icon) = std::move(value);
                } else if (type == BYTE && key == "hidden") {
                    found.hidden = reader.u8() != 0;
                } else {
                    reader.skip(type, 1);
                }
            }
            return found;
        }

        std::vector<Found> parse(const std::string& data) {
            std::vector<Found> out;
            Reader reader(data);
            if (reader.u8() != COMPOUND) {
                return out;
            }
            reader.text();
            for (uint8_t type = reader.u8(); type != END && !reader.bad; type = reader.u8()) {
                std::string key = reader.text();
                if (type != LIST || key != "servers") {
                    reader.skip(type, 1);
                    continue;
                }
                uint8_t inner = reader.u8();
                int32_t items = reader.count();
                for (int32_t i = 0; i < items && !reader.bad; i++) {
                    if (inner == COMPOUND) {
                        out.push_back(entry(reader));
                    } else {
                        reader.skip(inner, 1);
                    }
                }
            }
            return out;
        }

        std::shared_ptr<const Pixels> iconOf(const std::string& encoded) {
            if (encoded.empty()) {
                return nullptr;
            }
            std::optional<std::string> bytes = blb::base64_decode(encoded);
            if (!bytes || bytes->empty()) {
                return nullptr;
            }
            return Pixels::decode(bytes->data(), bytes->size());
        }

        std::vector<std::filesystem::path> files() {
            std::vector<std::filesystem::path> out;
            std::set<std::filesystem::path> seen;
            for (const Instances::Entry& entry : Instances::list()) {
                std::filesystem::path file = GameOptions::fileOf(entry).parent_path() / "servers.dat";
                if (seen.insert(file.lexically_normal()).second) {
                    out.push_back(file);
                }
            }
            return out;
        }

        void scan() {
            scanning = true;
            tasks.run(
                    [paths = files()](blb::cancellation_token token) -> Continuation {
                        std::vector<Server> found;
                        std::set<std::string> addresses;
                        for (const std::filesystem::path& file : paths) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            std::error_code error;
                            if (!std::filesystem::is_regular_file(file, error)) {
                                continue;
                            }
                            auto data = blb::fs::read_file(file);
                            if (!data) {
                                continue;
                            }
                            for (Found& item : parse(*data)) {
                                std::string address(blb::trim(item.address));
                                if (item.hidden || address.empty() ||
                                    !addresses.insert(blb::to_lower(address)).second) {
                                    continue;
                                }
                                std::string name(blb::trim(blb::strip_formatting(item.name)));
                                found.push_back({name, address, iconOf(item.icon)});
                            }
                        }
                        return [found = std::move(found)]() mutable {
                            list = std::move(found);
                            ++listRevision;
                            scanning = false;
                            scanned = true;
                            scannedAt = Clock::now();
                        };
                    },
                    [](const std::string&) -> Continuation {
                        return [] {
                            scanning = false;
                            scanned = true;
                            scannedAt = Clock::now();
                        };
                    });
        }
    }

    const std::vector<Server>& servers() {
        if (!scanning && !Instances::loading() && (!scanned || Clock::now() - scannedAt >= RESCAN)) {
            scan();
        }
        return list;
    }

    unsigned revision() {
        return listRevision;
    }

    bool loading() {
        return !scanned;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
