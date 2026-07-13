#include "file_hash_utils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <array>
#include <new>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace file_hash {
namespace {

constexpr std::size_t kReadBufferSize = 64 * 1024;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ull;

std::string HexDigest(const BYTE* data, DWORD size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result(static_cast<std::size_t>(size) * 2, '0');
    for (DWORD i = 0; i < size; ++i) {
        result[static_cast<std::size_t>(i) * 2] = kHex[data[i] >> 4];
        result[static_cast<std::size_t>(i) * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return result;
}

} // namespace

Result Compute(const std::filesystem::path& path, bool includeSha256) {
    Result result;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.fnvError = error;
        result.sha256Error = includeSha256 ? error : ERROR_SUCCESS;
        return result;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool sha256Active = false;
    if (includeSha256) {
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            result.sha256Error = GetLastError();
        } else if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
            result.sha256Error = GetLastError();
        } else {
            sha256Active = true;
        }
    }

    std::vector<BYTE> buffer;
    try {
        buffer.resize(kReadBufferSize);
    } catch (const std::bad_alloc&) {
        result.fnvError = ERROR_NOT_ENOUGH_MEMORY;
        if (includeSha256 && result.sha256Error == ERROR_SUCCESS) {
            result.sha256Error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    if (!buffer.empty()) {
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                const DWORD error = GetLastError();
                result.fnvError = error;
                if (sha256Active) {
                    result.sha256Error = error;
                    sha256Active = false;
                }
                break;
            }
            if (bytesRead == 0) {
                result.fnvOk = true;
                break;
            }

            for (DWORD i = 0; i < bytesRead; ++i) {
                result.fnv1a64 ^= buffer[i];
                result.fnv1a64 *= kFnv1a64Prime;
            }
            if (sha256Active && !CryptHashData(hash, buffer.data(), bytesRead, 0)) {
                result.sha256Error = GetLastError();
                sha256Active = false;
            }
        }
    }

    if (includeSha256 && result.fnvOk && sha256Active) {
        std::array<BYTE, 32> digest{};
        DWORD digestSize = static_cast<DWORD>(digest.size());
        if (CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0)) {
            result.sha256 = HexDigest(digest.data(), digestSize);
            result.sha256Ok = true;
        } else {
            result.sha256Error = GetLastError();
        }
    }

    if (hash) {
        CryptDestroyHash(hash);
    }
    if (provider) {
        CryptReleaseContext(provider, 0);
    }
    CloseHandle(file);
    return result;
}

} // namespace file_hash
