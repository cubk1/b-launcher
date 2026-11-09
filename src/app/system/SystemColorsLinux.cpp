#include "app/system/SystemColors.h"

#include <SDL3/SDL_video.h>

#include <dlfcn.h>

#include <chrono>

namespace SystemColors {

    namespace {
        struct DBusError {
            const char* name;
            const char* message;
            unsigned int dummy1 : 1;
            unsigned int dummy2 : 1;
            unsigned int dummy3 : 1;
            unsigned int dummy4 : 1;
            unsigned int dummy5 : 1;
            void* padding1;
        };

        struct alignas(16) Iter {
            unsigned char bytes[256];
        };

        constexpr int BUS_SESSION = 0;
        constexpr int TYPE_INVALID = 0;
        constexpr int TYPE_STRING = 's';
        constexpr int TYPE_VARIANT = 'v';
        constexpr int TYPE_STRUCT = 'r';
        constexpr int TYPE_DOUBLE = 'd';
        constexpr int TIMEOUT_MS = 200;
        constexpr auto INTERVAL = std::chrono::seconds(10);
        constexpr auto BACKOFF = std::chrono::seconds(60);

        struct Bus {
            void (*errorInit)(DBusError*) = nullptr;
            void (*errorFree)(DBusError*) = nullptr;
            void* (*busGet)(int, DBusError*) = nullptr;
            void (*connectionUnref)(void*) = nullptr;
            void* (*newCall)(const char*, const char*, const char*, const char*) = nullptr;
            unsigned int (*appendArgs)(void*, int, ...) = nullptr;
            void* (*sendBlock)(void*, void*, int, DBusError*) = nullptr;
            unsigned int (*iterInit)(void*, Iter*) = nullptr;
            int (*argType)(Iter*) = nullptr;
            void (*recurse)(Iter*, Iter*) = nullptr;
            void (*basic)(Iter*, void*) = nullptr;
            unsigned int (*next)(Iter*) = nullptr;
            void (*messageUnref)(void*) = nullptr;
            bool ready = false;
        };

        template <typename T>
        bool bind(void* library, T& out, const char* name) {
            out = reinterpret_cast<T>(dlsym(library, name));
            return out != nullptr;
        }

        Bus& bus() {
            static Bus loaded = [] {
                Bus out;
                void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
                if (library == nullptr) {
                    return out;
                }
                out.ready = bind(library, out.errorInit, "dbus_error_init") &&
                        bind(library, out.errorFree, "dbus_error_free") && bind(library, out.busGet, "dbus_bus_get") &&
                        bind(library, out.connectionUnref, "dbus_connection_unref") &&
                        bind(library, out.newCall, "dbus_message_new_method_call") &&
                        bind(library, out.appendArgs, "dbus_message_append_args") &&
                        bind(library, out.sendBlock, "dbus_connection_send_with_reply_and_block") &&
                        bind(library, out.iterInit, "dbus_message_iter_init") &&
                        bind(library, out.argType, "dbus_message_iter_get_arg_type") &&
                        bind(library, out.recurse, "dbus_message_iter_recurse") &&
                        bind(library, out.basic, "dbus_message_iter_get_basic") &&
                        bind(library, out.next, "dbus_message_iter_next") &&
                        bind(library, out.messageUnref, "dbus_message_unref");
                return out;
            }();
            return loaded;
        }

        bool accentOf(Bus& b, void* reply, uint32_t& argb) {
            Iter iter;
            if (!b.iterInit(reply, &iter)) {
                return false;
            }
            for (int depth = 0; depth < 3 && b.argType(&iter) == TYPE_VARIANT; depth++) {
                Iter inner;
                b.recurse(&iter, &inner);
                iter = inner;
            }
            if (b.argType(&iter) != TYPE_STRUCT) {
                return false;
            }
            Iter field;
            b.recurse(&iter, &field);
            double rgb[3] = {};
            int count = 0;
            while (count < 3 && b.argType(&field) == TYPE_DOUBLE) {
                b.basic(&field, &rgb[count]);
                count++;
                b.next(&field);
            }
            if (count != 3) {
                return false;
            }
            uint32_t out = 0xFF000000u;
            for (int i = 0; i < 3; i++) {
                if (!(rgb[i] >= 0.0 && rgb[i] <= 1.0)) {
                    return false;
                }
                out |= (uint32_t) (rgb[i] * 255.0 + 0.5) << (16 - 8 * i);
            }
            argb = out;
            return true;
        }

        bool readAccent(uint32_t& argb) {
            Bus& b = bus();
            if (!b.ready) {
                return false;
            }
            DBusError error;
            b.errorInit(&error);
            void* connection = b.busGet(BUS_SESSION, &error);
            b.errorFree(&error);
            if (connection == nullptr) {
                return false;
            }
            bool found = false;
            for (const char* method : {"ReadOne", "Read"}) {
                void* message = b.newCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                          "org.freedesktop.portal.Settings", method);
                if (message == nullptr) {
                    continue;
                }
                const char* scope = "org.freedesktop.appearance";
                const char* key = "accent-color";
                b.appendArgs(message, TYPE_STRING, &scope, TYPE_STRING, &key, TYPE_INVALID);
                b.errorInit(&error);
                void* reply = b.sendBlock(connection, message, TIMEOUT_MS, &error);
                b.errorFree(&error);
                b.messageUnref(message);
                if (reply == nullptr) {
                    continue;
                }
                found = accentOf(b, reply, argb);
                b.messageUnref(reply);
                if (found) {
                    break;
                }
            }
            b.connectionUnref(connection);
            return found;
        }

        bool accentKnown = false;
        uint32_t accent = 0;
        std::chrono::steady_clock::time_point nextQuery;
    }

    State read() {
        State state;
        SDL_SystemTheme theme = SDL_GetSystemTheme();
        if (theme != SDL_SYSTEM_THEME_UNKNOWN) {
            state.darkKnown = true;
            state.dark = theme == SDL_SYSTEM_THEME_DARK;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= nextQuery) {
            uint32_t value = 0;
            accentKnown = readAccent(value);
            if (accentKnown) {
                accent = value;
            }
            nextQuery = now + (accentKnown ? INTERVAL : BACKOFF);
        }
        state.accentKnown = accentKnown;
        state.accent = accent;
        return state;
    }
}
