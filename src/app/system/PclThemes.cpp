#include "app/system/PclThemes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

namespace PclThemes {

    namespace {
        const char* const PCL_KEY = "Software\\PCL";

        uint64_t getHash(const char* input) {
            uint64_t num = 5381ULL;
            for (const char* p = input; *p; p++) {
                num = (num << 5) ^ num ^ (unsigned char) (*p);
            }
            return num ^ 12218072394304324399ULL;
        }

        std::string getSecretKey(const std::string& key) {
            if (key.empty()) {
                return "@;$ Abv2";
            }
            uint64_t hashVal = getHash(key.c_str());
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long) hashVal);
            std::string s(buf);
            if (s.size() > 8) {
                return s.substr(0, 8);
            }
            std::string padded = s;
            while (padded.size() < 8) {
                padded += 'X';
            }
            return padded.substr(s.size()) + s;
        }

        std::string base64Encode(const std::vector<uint8_t>& data) {
            static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            int val = 0;
            int valb = -6;
            for (uint8_t c : data) {
                val = (val << 8) + c;
                valb += 8;
                while (valb >= 0) {
                    out += TABLE[(val >> valb) & 0x3F];
                    valb -= 6;
                }
            }
            if (valb > -6) {
                out += TABLE[((val << 8) >> (valb + 8)) & 0x3F];
            }
            while (out.size() % 4) {
                out += '=';
            }
            return out;
        }

        std::vector<uint8_t> desCbcEncrypt(const std::vector<uint8_t>& plain, const uint8_t key[8],
                                           const uint8_t iv[8]) {
            HCRYPTPROV hProv = 0;
            HCRYPTKEY hKey = 0;
            if (!CryptAcquireContextA(&hProv, nullptr, MS_ENHANCED_PROV_A, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
                return {};
            }
            struct {
                BLOBHEADER hdr;
                DWORD keySize;
                BYTE keyData[8];
            } keyBlob{};
            keyBlob.hdr.bType = PLAINTEXTKEYBLOB;
            keyBlob.hdr.bVersion = CUR_BLOB_VERSION;
            keyBlob.hdr.aiKeyAlg = CALG_DES;
            keyBlob.keySize = 8;
            std::memcpy(keyBlob.keyData, key, 8);
            if (!CryptImportKey(hProv, (BYTE*) &keyBlob, sizeof(keyBlob), 0, CRYPT_EXPORTABLE, &hKey)) {
                CryptReleaseContext(hProv, 0);
                return {};
            }
            DWORD mode = CRYPT_MODE_CBC;
            CryptSetKeyParam(hKey, KP_MODE, (BYTE*) &mode, 0);
            CryptSetKeyParam(hKey, KP_IV, (BYTE*) iv, 0);
            DWORD dataLen = (DWORD) plain.size();
            DWORD bufLen = (DWORD) ((dataLen / 8 + 1) * 8);
            std::vector<uint8_t> buf(plain.begin(), plain.end());
            buf.resize(bufLen);
            if (!CryptEncrypt(hKey, 0, TRUE, 0, buf.data(), &dataLen, bufLen)) {
                CryptDestroyKey(hKey);
                CryptReleaseContext(hProv, 0);
                return {};
            }
            buf.resize(dataLen);
            CryptDestroyKey(hKey);
            CryptReleaseContext(hProv, 0);
            return buf;
        }

        std::string secretEncrypt(const std::string& plainText, const std::string& key) {
            std::string sk = getSecretKey(key);
            uint8_t keyBytes[8] = {};
            std::memcpy(keyBytes, sk.c_str(), std::min<size_t>(sk.size(), 8));
            uint8_t ivBytes[8] = {};
            std::memcpy(ivBytes, "95168702", 8);
            std::vector<uint8_t> plain(plainText.begin(), plainText.end());
            return base64Encode(desCbcEncrypt(plain, keyBytes, ivBytes));
        }

        std::string readReg(HKEY root, const char* subKey, const char* valueName) {
            HKEY hKey;
            if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                return {};
            }
            DWORD type = 0;
            DWORD size = 0;
            std::string out;
            if (RegQueryValueExA(hKey, valueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0) {
                std::string buf(size, '\0');
                if (RegQueryValueExA(hKey, valueName, nullptr, &type, (BYTE*) buf.data(), &size) == ERROR_SUCCESS) {
                    while (!buf.empty() && buf.back() == '\0') {
                        buf.pop_back();
                    }
                    out = buf;
                }
            }
            RegCloseKey(hKey);
            return out;
        }

        bool writeReg(const char* valueName, const std::string& value) {
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_CURRENT_USER, PCL_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                &hKey, nullptr) != ERROR_SUCCESS) {
                return false;
            }
            LSTATUS status =
                    RegSetValueExA(hKey, valueName, 0, REG_SZ, (const BYTE*) value.c_str(), (DWORD) (value.size() + 1));
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }

        std::string hardwareId() {
            std::string lastConfig = readReg(HKEY_LOCAL_MACHINE, "SYSTEM\\HardwareConfig", "LastConfig");
            std::string identify = readReg(HKEY_CURRENT_USER, PCL_KEY, "Identify");
            if (lastConfig.empty() || identify.empty()) {
                return {};
            }
            std::string trimmed;
            for (char c : lastConfig) {
                if (c != '{' && c != '}') {
                    trimmed += (char) toupper((unsigned char) c);
                }
            }
            uint64_t hashValue = getHash((trimmed + identify).c_str());
            char hashHex[17];
            std::snprintf(hashHex, sizeof(hashHex), "%016llX", (unsigned long long) hashValue);
            char padded[17];
            std::memset(padded, '7', 16);
            padded[16] = '\0';
            size_t hexLen = std::strlen(hashHex);
            if (hexLen <= 16) {
                std::memcpy(padded + (16 - hexLen), hashHex, hexLen);
            } else {
                std::memcpy(padded, hashHex + (hexLen - 16), 16);
            }
            char formatted[20];
            std::snprintf(formatted, sizeof(formatted), "%.4s-%.4s-%.4s-%.4s", padded + 4, padded + 12, padded,
                          padded + 8);
            return formatted;
        }

        Status current;
    }

    bool installed() {
        std::string existing = readReg(HKEY_CURRENT_USER, PCL_KEY, "UiLauncherThemeHide2");
        return !existing.empty();
    }

    const Status& status() {
        return current;
    }

    void unlock() {
        current = Status();
        std::string id = hardwareId();
        if (id.empty()) {
            current.state = State::Failed;
            current.error = "无法获取 PCL 识别码，请确保 PCL 已经运行过至少一次";
            return;
        }
        current.id = id;
        std::string value = secretEncrypt("0|1|2|3|4|5|6|7|8|9|10|11|12|13|14", "PCL" + id);
        if (value.empty()) {
            current.state = State::Failed;
            current.error = "加密失败";
            return;
        }
        if (!writeReg("UiLauncherThemeHide2", value)) {
            current.state = State::Failed;
            current.error = "写入注册表失败";
            return;
        }
        writeReg("HintBuy", "True");
        current.state = State::Done;
    }
}
