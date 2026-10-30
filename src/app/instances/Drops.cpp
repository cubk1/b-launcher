#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/instances/Mods.h"
#include "app/system/Notices.h"

#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/modpack.hpp>
#include <core/modpack/mods.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Drops {

    namespace {
        enum class Kind { Mod, ResourcePack, ShaderPack, DataPack, Modpack, Unknown };

        struct Item {
            std::filesystem::path file;
            Kind kind = Kind::Unknown;
            blb::modpack::pack_manifest manifest;
        };

        Handlers handlers;
        HWND target = nullptr;
        bool oleReady = false;

        bool over = false;
        std::vector<std::filesystem::path> dragged;

        std::thread worker;
        std::mutex lock;
        bool classified = false;
        std::vector<Item> items;
        std::string dropTarget;

        std::string lower(std::string text) {
            for (char& c : text) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return text;
        }

        std::string nameOf(const std::filesystem::path& file) {
            return lower(blb::path_to_utf8(file.filename()));
        }

        bool endsWith(const std::string& text, const std::string& tail) {
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        }

        bool looksLikeMod(const std::string& name) {
            return endsWith(name, ".jar") || endsWith(name, ".jar.disabled") || endsWith(name, ".litemod");
        }

        bool looksLikePack(const std::string& name) {
            return endsWith(name, ".mrpack");
        }

        bool looksLikeZip(const std::string& name) {
            return endsWith(name, ".zip");
        }

        std::string targetInstance() {
            return handlers.target ? handlers.target() : Instances::selected();
        }

        bool accepting() {
            return !handlers.accepting || handlers.accepting();
        }

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

        bool acceptable(const std::vector<std::filesystem::path>& files) {
            return std::any_of(files.begin(), files.end(), [](const std::filesystem::path& file) {
                std::string name = nameOf(file);
                return looksLikeMod(name) || looksLikePack(name) || looksLikeZip(name);
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
            if (looksLikeMod(name)) {
                item.kind = Kind::Mod;
                return item;
            }
            if (!looksLikePack(name) && !looksLikeZip(name)) {
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
            bool modpack = looksLikePack(name);
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
                    path == "instance.cfg" || endsWith(path, "mmc-pack.json")) {
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

        bool importing = false;

        void classifyLater(std::vector<std::filesystem::path> files) {
            if (worker.joinable()) {
                worker.join();
            }
            {
                std::lock_guard<std::mutex> guard(lock);
                classified = false;
                items.clear();
                dropTarget = targetInstance();
            }
            worker = std::thread([files] {
                std::vector<Item> found;
                for (const std::filesystem::path& file : files) {
                    try {
                        found.push_back(classify(file));
                    } catch (const std::exception&) {
                        found.push_back(Item{file});
                    }
                }
                std::lock_guard<std::mutex> guard(lock);
                items = std::move(found);
                classified = true;
            });
        }

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

            HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
                dragged = filesOf(data);
                over = !dragged.empty() && accepting();
                *effect = over && acceptable(dragged) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
                *effect = over && acceptable(dragged) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DragLeave() override {
                over = false;
                dragged.clear();
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
                over = false;
                std::vector<std::filesystem::path> files = filesOf(data);
                dragged.clear();
                if (!accepting() || !acceptable(files)) {
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
                *effect = DROPEFFECT_COPY;
                SetForegroundWindow(target);
                classifyLater(std::move(files));
                return S_OK;
            }
        };

        Target dropTargetObject;

        std::string instanceName(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            return entry != nullptr ? entry->info.id : std::string();
        }

        bool loadsMods(const Instances::Entry& entry) {
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                if (blb::install::is_primary_loader(loader.type) ||
                    loader.type == blb::install::loader_type::liteloader) {
                    return true;
                }
            }
            return false;
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

        void act(const std::vector<Item>& found, const std::string& key) {
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
                if (loadsMods(*entry)) {
                    addAll(key, found, Kind::Mod, "mods", "模组");
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
        target = (HWND) window;
        HRESULT ole = OleInitialize(nullptr);
        oleReady = SUCCEEDED(ole);
        if (oleReady) {
            RegisterDragDrop(target, &dropTargetObject);
        }
    }

    void detach() {
        if (target != nullptr && oleReady) {
            RevokeDragDrop(target);
            OleUninitialize();
        }
        oleReady = false;
        target = nullptr;
        over = false;
        dragged.clear();
    }

    bool hovering() {
        return over;
    }

    std::string promise() {
        int mods = 0;
        int packs = 0;
        int zips = 0;
        for (const std::filesystem::path& file : dragged) {
            std::string name = nameOf(file);
            mods += looksLikeMod(name) ? 1 : 0;
            packs += looksLikePack(name) ? 1 : 0;
            zips += looksLikeZip(name) ? 1 : 0;
        }
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
        std::vector<Item> found;
        std::string key;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!classified) {
                return;
            }
            classified = false;
            found = std::move(items);
            items.clear();
            key = dropTarget;
        }
        if (worker.joinable()) {
            worker.join();
        }
        if (importing) {
            importing = false;
            for (const Item& item : found) {
                if (item.kind == Kind::Modpack) {
                    openModpack(item);
                    return;
                }
            }
            Notices::add(Notices::Kind::Failed, "这个文件不是支持的整合包", 4000);
            return;
        }
        act(found, key);
    }

    void importPack(const std::filesystem::path& file) {
        importing = true;
        classifyLater({file});
    }

    void shutdown() {
        if (worker.joinable()) {
            worker.join();
        }
        detach();
    }
}
