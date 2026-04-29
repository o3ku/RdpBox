#include "PasswordProtection.h"

#include "Win32String.h"

#include <mutex>
#include <string>
#include <vector>

#include <bcrypt.h>
#include <wincrypt.h>
#include <windows.h>

namespace
{
constexpr DWORD kPortableKdfIterations = 120000;
constexpr size_t kPortableKeyBytes = 32;
constexpr size_t kPortableNonceBytes = 12;
constexpr size_t kPortableTagBytes = 16;
constexpr BYTE kPortableSalt[] = {
    0x52, 0x64, 0x70, 0x42, 0x6F, 0x78, 0x2D, 0x50,
    0x6F, 0x72, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x31
};
constexpr wchar_t kPortablePassword[] = L"o3ku/rdpbox";

PasswordProtection::Mode g_mode = PasswordProtection::Mode::Dpapi;
bool g_ready = true;
std::vector<BYTE> g_portableKey;
std::mutex g_mutex;

bool ntSuccess(NTSTATUS status)
{
    return status >= 0;
}

std::string base64Encode(const BYTE *data, DWORD size)
{
    if (!data || size == 0)
        return {};

    DWORD encodedSize = 0;
    if (!::CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &encodedSize)
        || encodedSize == 0) {
        return {};
    }

    std::string encoded(encodedSize, '\0');
    if (!::CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &encodedSize))
        return {};

    if (!encoded.empty() && encoded.back() == '\0')
        encoded.pop_back();
    return encoded;
}

std::vector<BYTE> base64Decode(const std::string &text)
{
    if (text.empty())
        return {};

    DWORD decodedSize = 0;
    if (!::CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &decodedSize, nullptr, nullptr)
        || decodedSize == 0) {
        return {};
    }

    std::vector<BYTE> decoded(decodedSize);
    if (!::CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64, decoded.data(), &decodedSize, nullptr, nullptr))
        return {};

    decoded.resize(decodedSize);
    return decoded;
}

bool generateRandomBytes(BYTE *buffer, ULONG size)
{
    return buffer && size > 0
        && ntSuccess(::BCryptGenRandom(nullptr, buffer, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

bool derivePortableKeyBytes(const std::wstring &password,
                            const std::vector<BYTE> &salt,
                            std::vector<BYTE> &key)
{
    key.assign(kPortableKeyBytes, 0);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!ntSuccess(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return false;

    const NTSTATUS status = ::BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(password.data())),
        static_cast<ULONG>(password.size() * sizeof(wchar_t)),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        kPortableKdfIterations,
        key.data(),
        static_cast<ULONG>(key.size()),
        0);

    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    return ntSuccess(status);
}

bool encryptPortableBytes(const std::vector<BYTE> &key,
                          const std::vector<BYTE> &plainText,
                          std::vector<BYTE> &sealed)
{
    if (key.size() != kPortableKeyBytes)
        return false;

    std::vector<BYTE> nonce(kPortableNonceBytes);
    std::vector<BYTE> tag(kPortableTagBytes);
    if (!generateRandomBytes(nonce.data(), static_cast<ULONG>(nonce.size())))
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!ntSuccess(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;

    if (!ntSuccess(::BCryptSetProperty(algorithm,
                                        BCRYPT_CHAINING_MODE,
                                        reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                                        0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    DWORD objectLength = 0;
    DWORD cbResult = 0;
    if (!ntSuccess(::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&objectLength),
                                        sizeof(objectLength), &cbResult, 0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<BYTE> keyObject(objectLength);
    BCRYPT_KEY_HANDLE aesKey = nullptr;
    if (!ntSuccess(::BCryptGenerateSymmetricKey(algorithm, &aesKey, keyObject.data(), objectLength,
                                                 const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<BYTE> cipherText(plainText.size());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag = tag.data();
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    ULONG resultSize = 0;
    const NTSTATUS status = ::BCryptEncrypt(aesKey,
                                            const_cast<PUCHAR>(plainText.data()),
                                            static_cast<ULONG>(plainText.size()),
                                            &authInfo,
                                            nullptr,
                                            0,
                                            cipherText.data(),
                                            static_cast<ULONG>(cipherText.size()),
                                            &resultSize,
                                            0);

    ::BCryptDestroyKey(aesKey);
    ::BCryptCloseAlgorithmProvider(algorithm, 0);

    if (!ntSuccess(status))
        return false;

    cipherText.resize(resultSize);
    sealed.clear();
    sealed.insert(sealed.end(), nonce.begin(), nonce.end());
    sealed.insert(sealed.end(), tag.begin(), tag.end());
    sealed.insert(sealed.end(), cipherText.begin(), cipherText.end());
    return true;
}

bool decryptPortableBytes(const std::vector<BYTE> &key,
                          const std::vector<BYTE> &sealed,
                          std::vector<BYTE> &plainText)
{
    if (key.size() != kPortableKeyBytes || sealed.size() < (kPortableNonceBytes + kPortableTagBytes))
        return false;

    const BYTE *nonce = sealed.data();
    const BYTE *tag = sealed.data() + kPortableNonceBytes;
    const BYTE *cipherText = sealed.data() + kPortableNonceBytes + kPortableTagBytes;
    const size_t cipherSize = sealed.size() - kPortableNonceBytes - kPortableTagBytes;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!ntSuccess(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;

    if (!ntSuccess(::BCryptSetProperty(algorithm,
                                        BCRYPT_CHAINING_MODE,
                                        reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                                        0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    DWORD objectLength = 0;
    DWORD cbResult = 0;
    if (!ntSuccess(::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&objectLength),
                                        sizeof(objectLength), &cbResult, 0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<BYTE> keyObject(objectLength);
    BCRYPT_KEY_HANDLE aesKey = nullptr;
    if (!ntSuccess(::BCryptGenerateSymmetricKey(algorithm, &aesKey, keyObject.data(), objectLength,
                                                 const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0))) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    plainText.assign(cipherSize, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce = static_cast<ULONG>(kPortableNonceBytes);
    authInfo.pbTag = const_cast<PUCHAR>(tag);
    authInfo.cbTag = static_cast<ULONG>(kPortableTagBytes);

    ULONG resultSize = 0;
    const NTSTATUS status = ::BCryptDecrypt(aesKey,
                                            const_cast<PUCHAR>(cipherText),
                                            static_cast<ULONG>(cipherSize),
                                            &authInfo,
                                            nullptr,
                                            0,
                                            plainText.data(),
                                            static_cast<ULONG>(plainText.size()),
                                            &resultSize,
                                            0);

    ::BCryptDestroyKey(aesKey);
    ::BCryptCloseAlgorithmProvider(algorithm, 0);

    if (!ntSuccess(status))
        return false;

    plainText.resize(resultSize);
    return true;
}
}

namespace PasswordProtection
{
void setMode(Mode mode)
{
    std::scoped_lock lock(g_mutex);
    g_mode = mode;
    g_portableKey.clear();
    g_ready = true;

    if (mode == Mode::Portable) {
        std::vector<BYTE> salt(std::begin(kPortableSalt), std::end(kPortableSalt));
        g_ready = derivePortableKeyBytes(kPortablePassword, salt, g_portableKey);
    }
}

Mode mode()
{
    std::scoped_lock lock(g_mutex);
    return g_mode;
}

bool isReady()
{
    std::scoped_lock lock(g_mutex);
    return g_ready;
}

std::string protect(const std::wstring &plaintext)
{
    std::scoped_lock lock(g_mutex);
    return g_mode == Mode::Portable
        ? protectPortable(plaintext)
        : protectDpapi(plaintext);
}

std::wstring unprotectCurrent(const std::string &encoded, bool *ok)
{
    std::scoped_lock lock(g_mutex);
    return g_mode == Mode::Portable
        ? unprotectPortable(encoded, ok)
        : unprotectDpapi(encoded, ok);
}

std::string protectDpapi(const std::wstring &plaintext)
{
    if (plaintext.empty())
        return {};

    DATA_BLOB plain = {};
    plain.pbData = reinterpret_cast<BYTE *>(const_cast<wchar_t *>(plaintext.data()));
    plain.cbData = static_cast<DWORD>(plaintext.size() * sizeof(wchar_t));

    DATA_BLOB cipher = {};
    if (!::CryptProtectData(&plain, L"RdpBox Profile Password", nullptr, nullptr, nullptr, 0, &cipher))
        return {};

    const std::string encoded = base64Encode(cipher.pbData, cipher.cbData);
    ::LocalFree(cipher.pbData);
    return encoded;
}

std::wstring unprotectDpapi(const std::string &encoded, bool *ok)
{
    if (ok)
        *ok = false;

    const std::vector<BYTE> decoded = base64Decode(encoded);
    if (decoded.empty())
        return {};

    DATA_BLOB cipher = {};
    cipher.pbData = const_cast<BYTE *>(decoded.data());
    cipher.cbData = static_cast<DWORD>(decoded.size());

    DATA_BLOB plain = {};
    if (!::CryptUnprotectData(&cipher, nullptr, nullptr, nullptr, nullptr, 0, &plain))
        return {};

    std::wstring password(reinterpret_cast<wchar_t *>(plain.pbData),
                          plain.cbData / sizeof(wchar_t));
    ::LocalFree(plain.pbData);
    if (ok)
        *ok = true;
    return password;
}

std::string protectPortable(const std::wstring &plaintext)
{
    if (!g_ready || g_portableKey.empty() || plaintext.empty())
        return {};

    const std::string utf8 = utf8FromWide(plaintext);
    std::vector<BYTE> plainBytes(utf8.begin(), utf8.end());
    std::vector<BYTE> sealed;
    if (!encryptPortableBytes(g_portableKey, plainBytes, sealed))
        return {};

    return base64Encode(sealed.data(), static_cast<DWORD>(sealed.size()));
}

std::wstring unprotectPortable(const std::string &encoded, bool *ok)
{
    if (ok)
        *ok = false;

    if (!g_ready || g_portableKey.empty())
        return {};

    const std::vector<BYTE> sealed = base64Decode(encoded);
    if (sealed.empty())
        return {};

    std::vector<BYTE> plainBytes;
    if (!decryptPortableBytes(g_portableKey, sealed, plainBytes))
        return {};

    const std::string utf8(plainBytes.begin(), plainBytes.end());
    if (ok)
        *ok = true;
    return wideFromUtf8(utf8);
}
}
