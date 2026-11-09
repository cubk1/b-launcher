#include "ui/imgui/screens/theme/ThemeSounds.h"

#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include <algorithm>
#include <optional>

namespace ThemeSounds {

    namespace {
        constexpr size_t MAX_BYTES = (size_t) 1 << 20;
        static_assert(ThemePaintSpec::SOUND_KINDS == UiSound::KIND_COUNT);

        ThemePaintSpec::SoundSpec last[ThemePaintSpec::SOUND_KINDS];
        float lastVolume = -1.0f;
        bool synced = false;
        std::vector<std::string> issues;

        void apply(UiSound::Kind kind, const ThemePaintSpec::SoundSpec& sound, float volume) {
            if (sound.silent) {
                UiSound::silence(kind);
                return;
            }
            if (sound.source.empty()) {
                UiSound::restore(kind);
                return;
            }
            std::string owner;
            std::string path;
            std::optional<std::string> bytes;
            if (ThemePaintSpec::splitSource(sound.source, owner, path)) {
                bytes = ThemeStore::read(owner, path);
            }
            if (!bytes) {
                issues.push_back("找不到音效 " + path);
            } else if (bytes->size() > MAX_BYTES) {
                issues.push_back(path + " 超过 1 MB");
            } else if (!UiSound::theme(kind, (const unsigned char*) bytes->data(), (unsigned int) bytes->size(),
                                       volume)) {
                issues.push_back(path + " 不是有效的 WAV 文件");
            } else {
                return;
            }
            UiSound::restore(kind);
        }
    }

    void sync() {
        const ThemePaintSpec::Spec& spec = ThemePaintSpec::current();
        if (synced && lastVolume == spec.soundVolume &&
            std::equal(std::begin(last), std::end(last), std::begin(spec.sounds))) {
            return;
        }
        synced = true;
        lastVolume = spec.soundVolume;
        std::copy(std::begin(spec.sounds), std::end(spec.sounds), std::begin(last));
        issues.clear();
        for (int kind = 0; kind < ThemePaintSpec::SOUND_KINDS; kind++) {
            apply((UiSound::Kind) kind, spec.sounds[kind], spec.soundVolume);
        }
    }

    const std::vector<std::string>& problems() {
        return issues;
    }
}
