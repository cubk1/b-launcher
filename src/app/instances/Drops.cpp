#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/instances/Mods.h"
#include "app/system/Interface.h"
#include "app/system/Notices.h"

#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/modpack.hpp>
#include <core/modpack/mods.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Drops {

    namespace {
        enum class Kind { Mod, ResourcePack, ShaderPack, DataPack, Modpack, Unknown };

        struct Item {
            std::filesystem::path file;
            Kind kind = Kind::Unknown;
            blb::modpack::pack_manifest manifest;
        };

#if !defined(_WIN32)
        struct POINT {
            long x;
            long y;
        };
#endif

        Handlers handlers;
#if defined(_WIN32)
        HWND target = nullptr;
        bool oleReady = false;
#endif

        bool over = false;
        std::vector<std::filesystem::path> dragged;
        std::string draggedServer;
        std::vector<std::string> droppedServers;
        POINT hoverPoint{-1, -1};

        std::thread worker;
        std::mutex lock;
        std::condition_variable wake;
        bool stopping = false;
        struct Request {
            std::vector<std::filesystem::path> files;
            std::string target;
            bool importing = false;
            POINT point{-1, -1};
        };
        struct Batch {
            std::vector<Item> items;
            std::string target;
            bool importing = false;
            POINT point{-1, -1};
        };
        std::deque<Request> requests;
        std::deque<Batch> completed;

        std::string nameOf(const std::filesystem::path& file) {
            return blb::to_lower(blb::path_to_utf8(file.filename()));
        }

        std::string targetInstance() {
            return handlers.target ? handlers.target() : Instances::selected();
        }

        bool accepting() {
            return !handlers.accepting || handlers.accepting();
        }

        int hexDigit(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::string serverOf(std::string text) {
            constexpr std::string_view SCHEME = "authlib-injector:yggdrasil-server:";
            while (!text.empty() && (text.back() == '\0' || std::isspace((unsigned char) text.back()))) {
                text.pop_back();
            }
            size_t start = text.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || text.compare(start, SCHEME.size(), SCHEME) != 0) {
                return std::string();
            }
            std::string encoded = text.substr(start + SCHEME.size());
            std::string server;
            for (size_t i = 0; i < encoded.size(); i++) {
                int high = encoded[i] == '%' && i + 2 < encoded.size() ? hexDigit(encoded[i + 1]) : -1;
                int low = high >= 0 ? hexDigit(encoded[i + 2]) : -1;
                if (high >= 0 && low >= 0) {
                    server.push_back((char) (high * 16 + low));
                    i += 2;
                } else {
                    server.push_back(encoded[i]);
                }
            }
            if (server.empty() || server.find_first_of("\r\n") != std::string::npos) {
                return std::string();
            }
            return server;
        }

        std::string hostOf(const std::string& server) {
            size_t from = server.find("://");
            from = from == std::string::npos ? 0 : from + 3;
            size_t to = server.find('/', from);
            return server.substr(from, to == std::string::npos ? std::string::npos : to - from);
        }

#if defined(_WIN32)
        std::vector<std::filesystem::path> filesOf(IDataObject* data) {
            std::vector<std::filesystem::path> out;
            FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium = {};
            if (data == nullptr || FAILED(data->GetData(&format, &medium))) {
                return out;
            }
            HDROP drop = (HDROP) GlobalLock(medium.hGlobal);
            if (drop != nullptr) {
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; i++) {
                    UINT length = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring name(length, L' ');
                    DragQueryFileW(drop, i, name.data(), length + 1);
                    out.emplace_back(name);
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
            return out;
        }

        std::string serverIn(IDataObject* data) {
            FORMATETC format = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium = {};
            if (data == nullptr || FAILED(data->GetData(&format, &medium))) {
                return std::string();
            }
            std::wstring text;
            if (const auto* chars = (const wchar_t*) GlobalLock(medium.hGlobal)) {
                size_t limit = GlobalSize(medium.hGlobal) / sizeof(wchar_t);
                size_t length = 0;
                while (length < limit && length < 4096 && chars[length] != L'\0') {
                    length++;
                }
                text.assign(chars, length);
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
            std::string narrow;
            int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), nullptr, 0, nullptr, nullptr);
            if (size > 0) {
                narrow.resize((size_t) size);
                WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), narrow.data(), size, nullptr, nullptr);
            }
            return serverOf(narrow);
        }
#endif

        bool acceptable(const std::vector<std::filesystem::path>& files) {
            return std::any_of(files.begin(), files.end(), [](const std::filesystem::path& file) {
                std::string name = nameOf(file);
                return Mods::acceptsDrop(name) || name.ends_with(".mrpack") || name.ends_with(".zip");
            });
        }

        Item classify(const std::filesystem::path& file) {
            Item item;
            item.file = file;
            std::string name = nameOf(file);
            std::error_code error;
            if (std::filesystem::is_directory(file, error)) {
                return item;
            }
            if (Mods::acceptsDrop(name)) {
                item.kind = Kind::Mod;
                return item;
            }
            if (!name.ends_with(".mrpack") && !name.ends_with(".zip")) {
                return item;
            }
            auto zip = blb::zip_reader::open(file);
            if (!zip) {
                return item;
            }
            auto entries = zip->entries();
            if (!entries) {
                return item;
            }
            bool packMeta = false;
            bool assets = false;
            bool data = false;
            bool shaders = false;
            bool modpack = name.ends_with(".mrpack");
            for (const blb::zip_entry& entry : *entries) {
                const std::string& path = entry.name;
                if (path == "pack.mcmeta") {
                    packMeta = true;
                } else if (path.rfind("assets/", 0) == 0) {
                    assets = true;
                } else if (path.rfind("data/", 0) == 0) {
                    data = true;
                } else if (path.rfind("shaders/", 0) == 0) {
                    shaders = true;
                }
                if (path == "modrinth.index.json" || path == "manifest.json" || path == "mcbbs.packmeta" ||
                    path == "instance.cfg" || path.ends_with("mmc-pack.json")) {
                    modpack = true;
                }
            }
            if (modpack) {
                auto manifest = blb::modpack::read_modpack(file);
                if (manifest) {
                    item.kind = Kind::Modpack;
                    item.manifest = std::move(*manifest);
                    return item;
                }
            }
            if (shaders) {
                item.kind = Kind::ShaderPack;
            } else if (packMeta && (assets || !data)) {
                item.kind = Kind::ResourcePack;
            } else if (packMeta) {
                item.kind = Kind::DataPack;
            }
            return item;
        }

        void classifyRequests() {
            for (;;) {
                Request request;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [] { return stopping || !requests.empty(); });
                    if (stopping) return;
                    request = std::move(requests.front());
                    requests.pop_front();
                }
                Batch result{{}, std::move(request.target), request.importing, request.point};
                for (const std::filesystem::path& file : request.files) {
                    try {
                        result.items.push_back(classify(file));
                    } catch (const std::exception&) {
                        result.items.push_back(Item{file});
                    }
                }
                std::lock_guard<std::mutex> guard(lock);
                completed.push_back(std::move(result));
            }
        }

        void classifyLater(std::vector<std::filesystem::path> files, bool importing = false, POINT point = {-1, -1}) {
            Request request{std::move(files), targetInstance(), importing, point};
            {
                std::lock_guard<std::mutex> guard(lock);
                requests.push_back(std::move(request));
            }
            if (!worker.joinable()) {
                worker = std::thread(classifyRequests);
            }
            wake.notify_one();
        }

#if defined(_WIN32)
        class Target final : public IDropTarget {
        public:
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
                if (id == IID_IUnknown || id == IID_IDropTarget) {
                    *object = static_cast<IDropTarget*>(this);
                    AddRef();
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override {
                return 1;
            }

            ULONG STDMETHODCALLTYPE Release() override {
                return 1;
            }

            static void remember(POINTL screen) {
                POINT point{screen.x, screen.y};
                if (target != nullptr && ScreenToClient(target, &point)) {
                    hoverPoint = point;
                }
            }

            static bool takes() {
                return !draggedServer.empty() || acceptable(dragged);
            }

            HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL screen, DWORD* effect) override {
                remember(screen);
                dragged = filesOf(data);
                draggedServer = dragged.empty() ? serverIn(data) : std::string();
                over = (!dragged.empty() || !draggedServer.empty()) && accepting();
                *effect = over && takes() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL screen, DWORD* effect) override {
                remember(screen);
                *effect = over && takes() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DragLeave() override {
                over = false;
                dragged.clear();
                draggedServer.clear();
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL screen, DWORD* effect) override {
                remember(screen);
                over = false;
                std::vector<std::filesystem::path> files = filesOf(data);
                std::string server = files.empty() ? serverIn(data) : std::string();
                dragged.clear();
                draggedServer.clear();
                if (!server.empty() && accepting()) {
                    *effect = DROPEFFECT_COPY;
                    SetForegroundWindow(target);
                    droppedServers.push_back(std::move(server));
                    return S_OK;
                }
                if (!accepting() || !acceptable(files)) {
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
                *effect = DROPEFFECT_COPY;
                SetForegroundWindow(target);
                classifyLater(std::move(files), false, hoverPoint);
                return S_OK;
            }
        };

        Target dropTargetObject;
#endif

        std::string instanceName(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            return entry != nullptr ? entry->info.id : std::string();
        }

        void openModpack(const Item& item) {
            if (Installer::running()) {
                Notices::add(Notices::Kind::Failed, "正在安装，等它完成后再拖入整合包", 4000);
                return;
            }
            if (handlers.openModpack) {
                handlers.openModpack(item.file, item.manifest);
            }
        }

        void addAll(const std::string& key, const std::vector<Item>& found, Kind kind, const char* folder,
                    const char* what) {
            std::vector<std::filesystem::path> files;
            for (const Item& item : found) {
                if (item.kind == kind) {
                    files.push_back(item.file);
                }
            }
            if (!files.empty()) {
                Mods::add(key, files, folder, what);
            }
        }

        void act(const std::vector<Item>& found, const std::string& key, POINT point) {
            for (const Item& item : found) {
                if (item.kind == Kind::Modpack) {
                    openModpack(item);
                    return;
                }
            }
            auto count = [&](Kind kind) {
                return std::count_if(found.begin(), found.end(),
                                     [kind](const Item& item) { return item.kind == kind; });
            };
            const Instances::Entry* entry = Instances::find(key);
            bool anyFile = count(Kind::Mod) + count(Kind::ResourcePack) + count(Kind::ShaderPack) > 0;
            if (anyFile && entry == nullptr) {
                Notices::add(Notices::Kind::Failed, "还没有实例，先新建一个实例再添加", 4000);
                return;
            }
            if (count(Kind::Mod) > 0) {
                if (Instances::hasModLoader(*entry)) {
                    addAll(key, found, Kind::Mod, "mods", "模组");
                    if (handlers.placed && point.x >= 0) {
                        std::vector<std::filesystem::path> mods;
                        for (const Item& item : found) {
                            if (item.kind == Kind::Mod) {
                                mods.push_back(item.file);
                            }
                        }
                        handlers.placed(key, mods, (int) point.x, (int) point.y);
                    }
                } else {
                    Notices::add(Notices::Kind::Failed, entry->info.id + " 没有安装模组加载器，不能添加模组", 5000);
                }
            }
            if (entry != nullptr) {
                addAll(key, found, Kind::ResourcePack, "resourcepacks", "资源包");
                addAll(key, found, Kind::ShaderPack, "shaderpacks", "光影包");
            }
            if (count(Kind::DataPack) > 0) {
                Notices::add(Notices::Kind::Failed, "数据包要放进存档的 datapacks 文件夹，不能直接添加到实例", 5000);
            }
            for (const Item& item : found) {
                if (item.kind == Kind::Unknown) {
                    Notices::add(Notices::Kind::Failed, "无法识别 " + blb::path_to_utf8(item.file.filename()), 4000);
                    break;
                }
            }
        }
    }

    void attach(void* window, Handlers next) {
        handlers = std::move(next);
#if defined(_WIN32)
        target = (HWND) window;
        HRESULT ole = OleInitialize(nullptr);
        oleReady = SUCCEEDED(ole);
        if (oleReady) {
            RegisterDragDrop(target, &dropTargetObject);
        }
#else
        (void) window;
#endif
    }

    void detach() {
#if defined(_WIN32)
        if (target != nullptr && oleReady) {
            RevokeDragDrop(target);
            OleUninitialize();
        }
        oleReady = false;
        target = nullptr;
#endif
        over = false;
        dragged.clear();
        draggedServer.clear();
    }

    void hover(bool inside, int x, int y) {
        over = inside && accepting();
        hoverPoint = over ? POINT{x, y} : POINT{-1, -1};
        if (!over) {
            dragged.clear();
            draggedServer.clear();
        }
    }

    void droppedText(const std::string& text) {
        over = false;
        std::string server = serverOf(text);
        if (!server.empty() && accepting()) {
            droppedServers.push_back(std::move(server));
        }
    }

    void dropped(std::vector<std::filesystem::path> files, int x, int y) {
        over = false;
        if (!accepting() || !acceptable(files)) {
            return;
        }
        classifyLater(std::move(files), false, POINT{x, y});
    }

    bool hovering() {
        return over;
    }

    int modCount() {
        int mods = 0;
        for (const std::filesystem::path& file : dragged) {
            mods += Mods::acceptsDrop(nameOf(file)) ? 1 : 0;
        }
        return mods;
    }

    bool point(int& x, int& y) {
        if (!over || hoverPoint.x < 0) {
            return false;
        }
        x = (int) hoverPoint.x;
        y = (int) hoverPoint.y;
        return true;
    }

    std::string promise() {
        if (!draggedServer.empty()) {
            return "松开以登录 " + hostOf(draggedServer);
        }
        int mods = 0;
        int packs = 0;
        int zips = 0;
        for (const std::filesystem::path& file : dragged) {
            std::string name = nameOf(file);
            mods += Mods::acceptsDrop(name) ? 1 : 0;
            packs += name.ends_with(".mrpack") ? 1 : 0;
            zips += name.ends_with(".zip") ? 1 : 0;
        }
#if !defined(_WIN32)
        if (dragged.empty()) {
            return "松开以添加";
        }
#endif
        if (mods + packs + zips == 0) {
            return "不能添加这些文件";
        }
        if (packs > 0) {
            return "松开以安装整合包";
        }
        std::string instance = instanceName(targetInstance());
        if (instance.empty()) {
            return zips > 0 ? std::string("松开以安装整合包") : std::string("还没有实例，先新建一个实例");
        }
        if (zips == 0) {
            return "松开以添加 " + std::to_string(mods) + " 个模组到 " + instance;
        }
        return "松开以添加到 " + instance;
    }

    void poll() {
        for (const std::string& server : std::exchange(droppedServers, {})) {
            Interface::needAccount("yggdrasil", server);
        }
        std::deque<Batch> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(completed);
        }
        for (const Batch& batch : ready) {
            if (batch.importing) {
                auto pack = std::find_if(batch.items.begin(), batch.items.end(),
                                         [](const Item& item) { return item.kind == Kind::Modpack; });
                if (pack != batch.items.end()) {
                    openModpack(*pack);
                } else {
                    Notices::add(Notices::Kind::Failed, "这个文件不是支持的整合包", 4000);
                }
            } else {
                act(batch.items, batch.target, batch.point);
            }
        }
    }

    void importPack(const std::filesystem::path& file) {
        classifyLater({file}, true);
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
            requests.clear();
        }
        wake.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            completed.clear();
            stopping = false;
        }
        detach();
    }
}
