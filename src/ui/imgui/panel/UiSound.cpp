#include "UiSound.h"
#include "../theme/ThemeStyle.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#else
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace UiSound {

    bool enabled = true;

    namespace {
        std::vector<unsigned char> sounds[KIND_COUNT];
        std::vector<unsigned char> defaults[KIND_COUNT];
        bool themed[KIND_COUNT] = {};
        int requested = -1;
        bool requestedOwn = false;
#if !defined(_WIN32)
        SDL_AudioSpec specs[KIND_COUNT];
        std::vector<unsigned char> samples[KIND_COUNT];
        SDL_AudioStream* stream = nullptr;
        bool unavailable = false;

        bool decode(Kind kind) {
            samples[kind].clear();
            if (sounds[kind].empty()) {
                return false;
            }
            Uint8* buffer = nullptr;
            Uint32 length = 0;
            SDL_IOStream* io = SDL_IOFromConstMem(sounds[kind].data(), sounds[kind].size());
            if (io == nullptr || !SDL_LoadWAV_IO(io, true, &specs[kind], &buffer, &length)) {
                return false;
            }
            samples[kind].assign(buffer, buffer + length);
            SDL_free(buffer);
            return true;
        }

        void play(Kind kind) {
            if (unavailable || samples[kind].empty()) {
                return;
            }
            if (stream == nullptr) {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                    unavailable = true;
                    return;
                }
                stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specs[kind], nullptr, nullptr);
                if (stream == nullptr) {
                    unavailable = true;
                    return;
                }
                SDL_ResumeAudioStreamDevice(stream);
            }
            SDL_ClearAudioStream(stream);
            SDL_SetAudioStreamFormat(stream, &specs[kind], nullptr);
            SDL_PutAudioStreamData(stream, samples[kind].data(), (int) samples[kind].size());
        }
#endif

        uint32_t readU32(const unsigned char* p) {
            return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
        }

        uint16_t readU16(const unsigned char* p) {
            return (uint16_t) (p[0] | (p[1] << 8));
        }

        bool scale(std::vector<unsigned char>& wav, float volume) {
            if (wav.size() < 12 || std::memcmp(wav.data(), "RIFF", 4) != 0 ||
                std::memcmp(wav.data() + 8, "WAVE", 4) != 0) {
                return false;
            }
            bool pcm16 = false;
            size_t at = 12;
            while (at + 8 <= wav.size()) {
                uint32_t size = readU32(wav.data() + at + 4);
                size_t body = at + 8;
                if (body + size > wav.size()) {
                    break;
                }
                if (std::memcmp(wav.data() + at, "fmt ", 4) == 0 && size >= 16) {
                    pcm16 = readU16(wav.data() + body) == 1 && readU16(wav.data() + body + 14) == 16;
                } else if (std::memcmp(wav.data() + at, "data", 4) == 0) {
                    if (!pcm16) {
                        return false;
                    }
                    for (size_t i = body; i + 1 < body + size; i += 2) {
                        int16_t sample = (int16_t) readU16(wav.data() + i);
                        int scaled = (int) std::lround(sample * volume);
                        scaled = std::max(-32768, std::min(32767, scaled));
                        wav[i] = (unsigned char) (scaled & 0xFF);
                        wav[i + 1] = (unsigned char) ((scaled >> 8) & 0xFF);
                    }
                    return true;
                }
                at = body + size + (size & 1);
            }
            return false;
        }

        bool wave(const std::vector<unsigned char>& data) {
            return data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 &&
                    std::memcmp(data.data() + 8, "WAVE", 4) == 0;
        }

        void activate(Kind kind, const std::vector<unsigned char>& data) {
#if defined(_WIN32)
            PlaySoundW(nullptr, nullptr, 0);
#endif
            sounds[kind] = data;
#if !defined(_WIN32)
            decode(kind);
#endif
        }

        bool has(const std::string& label, std::initializer_list<const char*> words) {
            for (const char* word : words) {
                if (label.find(word) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }
    }

    void load(Kind kind, const unsigned char* wav, unsigned int size, float volume) {
        if (kind < 0 || kind >= KIND_COUNT) {
            return;
        }
        std::vector<unsigned char> data(wav, wav + size);
        defaults[kind] = scale(data, volume) ? std::move(data) : std::vector<unsigned char>();
        if (!themed[kind]) {
            activate(kind, defaults[kind]);
        }
    }

    bool theme(Kind kind, const unsigned char* wav, unsigned int size, float volume) {
        if (kind < 0 || kind >= KIND_COUNT) {
            return false;
        }
        std::vector<unsigned char> data(wav, wav + size);
        if (!wave(data)) {
            return false;
        }
        if (volume != 1.0f) {
            scale(data, volume);
        }
        themed[kind] = true;
        activate(kind, data);
        return true;
    }

    void silence(Kind kind) {
        if (kind < 0 || kind >= KIND_COUNT) {
            return;
        }
        themed[kind] = true;
        activate(kind, {});
    }

    void restore(Kind kind) {
        if (kind < 0 || kind >= KIND_COUNT || (!themed[kind] && sounds[kind] == defaults[kind])) {
            return;
        }
        themed[kind] = false;
        activate(kind, defaults[kind]);
    }

    void request(Kind kind) {
        requested = kind;
        requestedOwn = true;
    }

    void requestDefault() {
        if (!requestedOwn) {
            requested = PRESS;
        }
    }

    void flush() {
        int kind = requested;
        requested = -1;
        requestedOwn = false;
        if (!enabled || !ThemeStyle::sounds() || kind < 0 || sounds[kind].empty()) {
            return;
        }
#if defined(_WIN32)
        PlaySoundW((LPCWSTR) sounds[kind].data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
        play((Kind) kind);
#endif
    }

    Kind forLabel(const std::string& label) {
        if (has(label, {"取消", "删除", "关闭", "清除", "清空", "移除", "退出", "结束", "停止"})) {
            return RELEASE;
        }
        if (has(label, {"添加", "保存", "创建", "新建", "安装", "导入", "发送", "提交", "登录", "下载", "启动"})) {
            return PULSE;
        }
        return PRESS;
    }
}
