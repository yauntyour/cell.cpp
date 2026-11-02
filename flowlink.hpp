// flowlink.hpp — FlowLink (part of the cell.cpp core): peer discovery, pairing and end-to-end
// encrypted messaging between cell instances on the same network.
//
//   cell A  <-- UDP discovery (broadcast + multicast) -->  cell B
//   cell A  <-- TCP, X25519 + Argon2id pairing code ---->  cell B
//           <-- XChaCha20-Poly1305 on every frame ---->
//
// Design notes
// ------------
// * The service is OPT-IN and mounted on demand: nothing here is constructed
//   (no thread, no socket, no allocation) until cell::link::enable() runs.
//   `service` owns its own io_context, so it never shares a thread with the
//   agent loop, the UI or the HTTP server.
//
// * Identity: client_id = 16 random bytes from libsodium, 32 lowercase hex
//   characters. It names the peer directory `<exe dir>/.cell/links/<client_id>/`
//   and is the value the `cell` field of the tw tool carries.
//
// * Pairing (TLS-like, but with no PKI and no signatures — the pairing code IS
//   the credential): both sides derive the same Argon2id PSK from the 512-bit
//   code, do an ephemeral X25519 exchange, mix both into a handshake secret and
//   prove it by AEAD-decrypting each other's CONFIRM frame. Only a holder of
//   the code can produce a valid CONFIRM, so a wrong code fails the handshake
//   before a single byte of data is exchanged. The initiator then hands over
//   the long-term link key inside the already-encrypted handshake, and both
//   sides store it encrypted (same Argon2id + AES-256-GCM scheme as the API-key
//   vault) in `.cell/links/<peer client_id>/key`.
//
// * Every later session ("resume") uses that link key as the PSK, so pairing
//   happens once per pair of cells. Ephemeral keys give each session forward
//   secrecy; the link key is only ever a PSK, never a traffic key.
//
// * Frames: 4-byte big-endian length + JSON body. Before the handshake the body
//   is plaintext (HELLO / HELLO_ACK / CONFIRM); afterwards every frame is an
//   AEAD blob (`{"t":"enc","n":...,"c":...}`) — "packet encryption only", no
//   plaintext control channel to spoof.
#pragma once

#include <sodium.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/multicast.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <format>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h> // chmod — owner-only key files
#endif

namespace cell::flowlink
{
    namespace asio = boost::asio;
    using asio::ip::tcp;
    using asio::ip::udp;
    using json = nlohmann::json;

    // ---- protocol constants -------------------------------------------------
    inline constexpr const char *kProto = "CELLFLNK";
    inline constexpr int kVersion = 1;
    inline constexpr std::uint16_t kDefaultPort = 47822;          // TCP transport
    inline constexpr std::uint16_t kDefaultDiscoveryPort = 47821; // UDP discovery
    inline constexpr const char *kGroup = "239.255.42.71";        // multicast fallback
    inline constexpr std::size_t kMaxFrame = 16u * 1024u * 1024u; // 16 MiB
    inline constexpr std::size_t kMaxDatagram = 1024;
    inline constexpr std::size_t kPairingBytes = 64;              // 512-bit code
    inline constexpr std::size_t kKeyBytes = 32;
    inline constexpr int kAnnounceMillis = 5000;  // discovery beacon period
    inline constexpr int kPeerTtlSeconds = 30;    // discovered peer goes offline
    inline constexpr int kSessionIdleSeconds = 180; // idle TCP session is dropped
    inline constexpr int kPairTtlSeconds = 300;   // default pairing window
    inline constexpr int kPairMaxFails = 3;       // wrong codes before the window closes

    // ---- JSON readers that never throw -------------------------------------
    // Everything arriving over the network goes through these: nlohmann's
    // value() raises a type_error when the stored type differs from the default,
    // and an exception escaping an asio handler would end that io thread (a
    // remote peer could send {"port":"x"} and take the service down).
    inline std::string jstr(const json &j, const char *key, const std::string &def = {})
    {
        const auto it = j.find(key);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
    }
    inline long long jint(const json &j, const char *key, long long def = 0)
    {
        const auto it = j.find(key);
        if (it == j.end())
            return def;
        if (it->is_number_unsigned())
            return (long long)it->get<unsigned long long>();
        if (it->is_number_integer())
            return it->get<long long>();
        if (it->is_number_float())
            return (long long)it->get<double>();
        return def;
    }
    inline bool jbool(const json &j, const char *key, bool def = false)
    {
        const auto it = j.find(key);
        return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
    }

    // ---- small utilities ----------------------------------------------------
    inline std::string random_bytes(std::size_t n)
    {
        std::string out(n, '\0');
        if (n)
            randombytes_buf(out.data(), n);
        return out;
    }

    inline std::string hex_encode(std::string_view raw)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(raw.size() * 2);
        for (unsigned char c : raw)
        {
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
        return out;
    }

    inline bool is_hex_id(std::string_view s, std::size_t len)
    {
        if (s.size() != len)
            return false;
        for (char c : s)
            if (!std::isxdigit((unsigned char)c))
                return false;
        return true;
    }

    inline std::string b64_encode(std::string_view raw)
    {
        if (raw.empty())
            return {};
        std::string out(sodium_base64_encoded_len(raw.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
        sodium_bin2base64(out.data(), out.size(), (const unsigned char *)raw.data(), raw.size(),
                          sodium_base64_VARIANT_ORIGINAL);
        out.resize(std::strlen(out.c_str()));
        return out;
    }

    inline bool b64_decode(std::string_view text, std::string &out)
    {
        out.clear();
        if (text.empty())
            return false;
        std::string buf(text.size(), '\0');
        std::size_t len = 0;
        if (sodium_base642bin((unsigned char *)buf.data(), buf.size(), text.data(), text.size(),
                              nullptr, &len, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0)
            return false;
        buf.resize(len);
        out = std::move(buf);
        return true;
    }

    // base62 (0-9 A-Z a-z) — the pairing code alphabet. Big-integer conversion
    // over a byte string; a 64-byte secret encodes to 87 characters, which the
    // UI groups into 5-character blocks for reading out loud.
    inline constexpr char kBase62[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    inline std::string base62_encode(std::string_view raw)
    {
        std::vector<unsigned char> num(raw.begin(), raw.end());
        std::string out;
        while (!num.empty())
        {
            unsigned rem = 0;
            std::vector<unsigned char> q;
            q.reserve(num.size());
            for (unsigned char b : num)
            {
                const unsigned acc = (rem << 8) | b;
                const unsigned d = acc / 62u;
                rem = acc % 62u;
                if (!q.empty() || d != 0)
                    q.push_back((unsigned char)d);
            }
            out.push_back(kBase62[rem]);
            num.swap(q);
        }
        std::reverse(out.begin(), out.end());
        return out.empty() ? std::string("0") : out;
    }

    inline std::optional<std::string> base62_decode(std::string_view text)
    {
        std::vector<unsigned char> num; // big-endian
        for (char ch : text)
        {
            const char *at = std::strchr(kBase62, ch);
            if (!at || ch == '\0')
                return std::nullopt;
            unsigned carry = (unsigned)(at - kBase62);
            for (auto it = num.rbegin(); it != num.rend(); ++it)
            {
                const unsigned acc = (unsigned)(*it) * 62u + carry;
                *it = (unsigned char)(acc & 0xff);
                carry = acc >> 8;
            }
            while (carry)
            {
                num.insert(num.begin(), (unsigned char)(carry & 0xff));
                carry >>= 8;
            }
        }
        return std::string(num.begin(), num.end());
    }

    // strip separators/spaces: the code is read out in blocks and often pasted
    // with the grouping spaces intact
    inline std::string pairing_normalize(std::string_view in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
            if (std::isalnum((unsigned char)c))
                out.push_back(c);
        return out;
    }

    inline std::string pairing_code_new()
    {
        return base62_encode(random_bytes(kPairingBytes));
    }

    // decode a normalized code back to the fixed 64-byte secret (leading zero
    // bytes are lost by the base conversion, so left-pad)
    inline bool pairing_code_decode(const std::string &norm, std::string &raw)
    {
        if (norm.size() < 80 || norm.size() > 96)
            return false;
        auto bytes = base62_decode(norm);
        if (!bytes || bytes->size() > kPairingBytes)
            return false;
        raw.assign(kPairingBytes - bytes->size(), '\0');
        raw += *bytes;
        return true;
    }

    // "ABC12 DEF34 ..." for reading out loud / typing
    inline std::string pairing_display(const std::string &norm, std::size_t group = 5)
    {
        std::string out;
        for (std::size_t i = 0; i < norm.size(); i += group)
        {
            if (!out.empty())
                out.push_back(' ');
            out.append(norm.substr(i, std::min(group, norm.size() - i)));
        }
        return out;
    }

    inline std::string now_iso(long long epoch = 0)
    {
        const std::time_t t = (std::time_t)(epoch ? epoch : std::time(nullptr));
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }

    inline std::string default_name()
    {
        for (const char *var : {"CELL_FLOWLINK_NAME", "COMPUTERNAME", "HOSTNAME"})
            if (const char *v = std::getenv(var); v && *v)
                return std::string(v);
        return "cell";
    }

    // a display name must be safe to print and to put in JSON/log lines
    inline std::string clean_name(std::string s, std::size_t max_len = 48)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (c < 0x20 || c == 0x7f)
                continue;
            out.push_back((char)c);
            if (out.size() >= max_len)
                break;
        }
        // trim
        while (!out.empty() && out.front() == ' ')
            out.erase(out.begin());
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    // ---- crypto -------------------------------------------------------------
    inline std::string sha256(std::string_view in)
    {
        std::string out(crypto_hash_sha256_BYTES, '\0');
        crypto_hash_sha256((unsigned char *)out.data(), (const unsigned char *)in.data(), in.size());
        return out;
    }

    // generic HMAC-SHA256 (libsodium's crypto_auth_hmacsha256 wants a 32-byte
    // key; HKDF feeds it arbitrary-length ones)
    inline std::string hmac_sha256(std::string_view key, std::string_view msg)
    {
        std::string k(key);
        if (k.size() > 64)
            k = sha256(k);
        k.resize(64, '\0');
        std::string ipad(64, '\0'), opad(64, '\0');
        for (std::size_t i = 0; i < 64; i++)
        {
            ipad[i] = (char)(k[i] ^ 0x36);
            opad[i] = (char)(k[i] ^ 0x5c);
        }
        std::string inner = ipad;
        inner.append(msg);
        std::string outer = opad;
        outer += sha256(inner);
        sodium_memzero(k.data(), k.size());
        sodium_memzero(ipad.data(), ipad.size());
        sodium_memzero(opad.data(), opad.size());
        return sha256(outer);
    }

    inline std::string hkdf_extract(std::string_view salt, std::string_view ikm)
    {
        const std::string s = salt.empty() ? std::string(32, '\0') : std::string(salt);
        return hmac_sha256(s, ikm);
    }

    inline std::string hkdf_expand(std::string_view prk, std::string_view info, std::size_t len)
    {
        std::string out, t;
        for (unsigned char i = 1; out.size() < len; i++)
        {
            t = hmac_sha256(prk, t + std::string(info) + std::string(1, (char)i));
            out += t;
            if (i == 255)
                break;
        }
        out.resize(len);
        return out;
    }

    // XChaCha20-Poly1305 with a random 192-bit nonce — safe for an unbounded
    // number of frames under one key, which is exactly what a long-lived
    // session needs.
    inline json aead_seal(std::string_view key, std::string_view aad, std::string_view plain)
    {
        std::string nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, '\0');
        randombytes_buf(nonce.data(), nonce.size());
        std::string ct(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
        unsigned long long clen = 0;
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            (unsigned char *)ct.data(), &clen,
            (const unsigned char *)plain.data(), plain.size(),
            (const unsigned char *)aad.data(), aad.size(),
            nullptr, (const unsigned char *)nonce.data(), (const unsigned char *)key.data());
        ct.resize((std::size_t)clen);
        return json{{"n", b64_encode(nonce)}, {"c", b64_encode(ct)}};
    }

    inline bool aead_open(std::string_view key, std::string_view aad, const json &sealed, std::string &out)
    {
        out.clear();
        if (!sealed.is_object() || !sealed.contains("n") || !sealed.contains("c"))
            return false;
        if (!sealed["n"].is_string() || !sealed["c"].is_string())
            return false;
        std::string nonce, ct;
        if (!b64_decode(sealed["n"].get<std::string>(), nonce) || !b64_decode(sealed["c"].get<std::string>(), ct))
            return false;
        if (nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES ||
            ct.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES)
            return false;
        std::string plain(ct.size(), '\0');
        unsigned long long mlen = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                (unsigned char *)plain.data(), &mlen, nullptr,
                (const unsigned char *)ct.data(), ct.size(),
                (const unsigned char *)aad.data(), aad.size(),
                (const unsigned char *)nonce.data(), (const unsigned char *)key.data()) != 0)
            return false;
        plain.resize((std::size_t)mlen);
        out = std::move(plain);
        return true;
    }

    struct x25519_pair
    {
        std::string pub; // 32 bytes
        std::string sec; // 32 bytes
    };

    inline x25519_pair x25519_new()
    {
        x25519_pair k;
        k.pub.assign(crypto_kx_PUBLICKEYBYTES, '\0');
        k.sec.assign(crypto_kx_SECRETKEYBYTES, '\0');
        crypto_kx_keypair((unsigned char *)k.pub.data(), (unsigned char *)k.sec.data());
        return k;
    }

    inline bool x25519_shared(const std::string &sec, const std::string &peer_pub, std::string &out)
    {
        out.clear();
        if (sec.size() != crypto_scalarmult_curve25519_SCALARBYTES ||
            peer_pub.size() != crypto_scalarmult_curve25519_BYTES)
            return false;
        std::string dh(crypto_scalarmult_curve25519_BYTES, '\0');
        if (crypto_scalarmult((unsigned char *)dh.data(), (const unsigned char *)sec.data(),
                              (const unsigned char *)peer_pub.data()) != 0)
            return false; // low-order point
        out = std::move(dh);
        return true;
    }

    // Argon2id — used for the pairing PSK only (never per frame: it is
    // deliberately slow).
    inline std::string argon2id(std::string_view pass, std::string_view salt)
    {
        if (salt.size() != crypto_pwhash_SALTBYTES)
            return {};
        std::string out(kKeyBytes, '\0');
        if (crypto_pwhash((unsigned char *)out.data(), out.size(),
                          pass.data(), pass.size(),
                          (const unsigned char *)salt.data(),
                          crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                          crypto_pwhash_ALG_ARGON2ID13) != 0)
            return {};
        return out;
    }

    // the pairing salt both sides can compute: derived from the two client ids,
    // order-independent, so neither side has to send it
    inline std::string pair_salt(const std::string &a, const std::string &b)
    {
        const std::string lo = std::min(a, b), hi = std::max(a, b);
        return sha256("cell-flowlink-pair|" + lo + "|" + hi).substr(0, crypto_pwhash_SALTBYTES);
    }

    inline bool const_eq(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && (a.empty() || sodium_memcmp(a.data(), b.data(), a.size()) == 0);
    }

    // Owner-only permissions for a secret file, portably: Windows has no mode
    // bits, so the read-only attribute is the closest equivalent (the ACL side
    // is cell::plat::restrict_file_permissions' job for the vault); POSIX gets
    // 0600 regardless of the umask.
    inline void set_owner_only(const std::filesystem::path &p)
    {
#ifdef _WIN32
        std::error_code ec;
        std::filesystem::permissions(p,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
#else
        ::chmod(p.c_str(), S_IRUSR | S_IWUSR);
#endif
    }

    // ---- sealed key files ---------------------------------------------------
    // The link key is stored exactly like an API key in the vault: Argon2id over
    // the master secret (`.cell/.key`, or CELL_VAULT_PASSPHRASE) with a per-file
    // salt, then AES-256-GCM (XChaCha20-Poly1305 where AES-GCM is unavailable).
    // Format-compatible with `cell::encrypt::crypt`, one secret per file.
    struct sealed_file
    {
        static std::filesystem::path master_file(const std::filesystem::path &root) { return root / ".key"; }

        static bool load_master(const std::filesystem::path &root, std::string &master)
        {
            if (const char *p = std::getenv("CELL_VAULT_PASSPHRASE"); p && *p)
            {
                master = p;
                return true;
            }
            std::ifstream f(master_file(root), std::ios::binary);
            if (f)
            {
                std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                std::string raw;
                if (b64_decode(text, raw) && raw.size() == 32)
                {
                    master = std::move(raw);
                    return true;
                }
            }
            // first use: create the master secret the vault will share
            master = random_bytes(32);
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            std::ofstream out(master_file(root), std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << b64_encode(master);
            const bool ok = out.good();
            out.close();
            // owner-only from the moment it exists
            set_owner_only(master_file(root));
            return ok;
        }

        static bool derive(const std::filesystem::path &root, const std::string &salt, std::string &key)
        {
            std::string master;
            if (!load_master(root, master))
                return false;
            key = argon2id(master, salt);
            sodium_memzero(master.data(), master.size());
            return key.size() == kKeyBytes;
        }

        static bool write(const std::filesystem::path &root, const std::filesystem::path &file,
                          const std::string &plain)
        {
            std::string salt(crypto_pwhash_SALTBYTES, '\0');
            randombytes_buf(salt.data(), salt.size());
            std::string key;
            if (!derive(root, salt, key))
                return false;
            json sealed;
            const bool aes = crypto_aead_aes256gcm_is_available() != 0;
            if (aes)
            {
                std::string nonce(crypto_aead_aes256gcm_NPUBBYTES, '\0');
                randombytes_buf(nonce.data(), nonce.size());
                std::string ct(plain.size() + crypto_aead_aes256gcm_ABYTES, '\0');
                unsigned long long clen = 0;
                crypto_aead_aes256gcm_encrypt((unsigned char *)ct.data(), &clen,
                                              (const unsigned char *)plain.data(), plain.size(),
                                              nullptr, 0, nullptr,
                                              (const unsigned char *)nonce.data(),
                                              (const unsigned char *)key.data());
                ct.resize((std::size_t)clen);
                sealed = json{{"nonce", b64_encode(nonce)}, {"ct", b64_encode(ct)}};
            }
            else
            {
                sealed = aead_seal(key, {}, plain);
            }
            sodium_memzero(key.data(), key.size());
            json doc{{"version", 2},
                     {"kdf", "argon2id"},
                     {"aead", aes ? "aes256gcm" : "xchacha20poly1305"},
                     {"salt", b64_encode(salt)},
                     {"nonce", sealed["n"]},
                     {"ct", sealed["c"]},
                     {"created_at", (long long)std::time(nullptr)}};
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << doc.dump(2) << "\n";
            const bool ok = out.good();
            out.close();
            set_owner_only(file);
            return ok;
        }

        static bool read(const std::filesystem::path &root, const std::filesystem::path &file, std::string &plain)
        {
            plain.clear();
            std::ifstream f(file, std::ios::binary);
            if (!f)
                return false;
            json doc;
            try
            {
                doc = json::parse(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), nullptr, false);
            }
            catch (const std::exception &)
            {
                return false;
            }
            if (!doc.is_object() || !doc.contains("salt") || !doc["salt"].is_string())
                return false;
            std::string salt;
            if (!b64_decode(doc["salt"].get<std::string>(), salt))
                return false;
            std::string key;
            if (!derive(root, salt, key))
                return false;
            bool ok = false;
            const std::string aead = doc.value("aead", "");
            if (aead == "aes256gcm")
            {
                std::string nonce, ct;
                if (b64_decode(doc.value("nonce", ""), nonce) && b64_decode(doc.value("ct", ""), ct) &&
                    nonce.size() == crypto_aead_aes256gcm_NPUBBYTES && ct.size() >= crypto_aead_aes256gcm_ABYTES)
                {
                    std::string buf(ct.size(), '\0');
                    unsigned long long mlen = 0;
                    if (crypto_aead_aes256gcm_decrypt((unsigned char *)buf.data(), &mlen, nullptr,
                                                      (const unsigned char *)ct.data(), ct.size(),
                                                      nullptr, 0, (const unsigned char *)nonce.data(),
                                                      (const unsigned char *)key.data()) == 0)
                    {
                        buf.resize((std::size_t)mlen);
                        plain = std::move(buf);
                        ok = true;
                    }
                }
            }
            else
            {
                json sealed{{"n", doc.value("nonce", "")}, {"c", doc.value("ct", "")}};
                ok = aead_open(key, {}, sealed, plain);
            }
            sodium_memzero(key.data(), key.size());
            return ok;
        }
    };

    // ---- peer records and the on-disk store ---------------------------------
    struct peer_meta
    {
        std::string id;
        std::string name;
        std::string addr;       // last known address
        std::uint16_t port = 0; // last known TCP port
        long long paired_at = 0;
        long long last_seen = 0;
        std::string via = "code"; // "code" = we showed the code, "entered" = we typed theirs
        bool paired = false;

        json to_json() const
        {
            return {{"client_id", id}, {"name", name}, {"addr", addr}, {"port", port},
                    {"paired_at", paired_at}, {"last_seen", last_seen}, {"via", via}, {"paired", paired}};
        }
        static peer_meta from_json(const json &j)
        {
            peer_meta m;
            if (!j.is_object())
                return m;
            m.id = j.value("client_id", "");
            m.name = j.value("name", "");
            m.addr = j.value("addr", "");
            m.port = (std::uint16_t)std::clamp(j.value("port", 0), 0, 65535);
            m.paired_at = j.value("paired_at", (long long)0);
            m.last_seen = j.value("last_seen", (long long)0);
            m.via = j.value("via", std::string("code"));
            m.paired = j.value("paired", false);
            return m;
        }
    };

    struct mail_item
    {
        std::string id;
        std::string from;      // sender client_id
        std::string from_name; // sender display name
        std::string subject;
        std::string note;
        std::string kind = "text"; // "text" | "session"
        long long created = 0;
        bool read = false;
        json payload = json::object(); // transcript / body blocks
    };

    inline mail_item mail_from_json(const json &j)
    {
        mail_item m;
        if (!j.is_object())
            return m;
        m.id = j.value("id", "");
        m.from = j.value("from", "");
        m.from_name = j.value("from_name", "");
        m.subject = j.value("subject", "");
        m.note = j.value("note", "");
        m.kind = j.value("kind", std::string("text"));
        m.created = j.value("created", (long long)0);
        m.read = j.value("read", false);
        if (j.contains("payload"))
            m.payload = j["payload"];
        return m;
    }

    struct store
    {
        std::filesystem::path root; // the .cell directory

        std::filesystem::path links() const { return root / "links"; }
        std::filesystem::path link_dir(const std::string &id) const { return links() / id; }
        std::filesystem::path key_file(const std::string &id) const { return link_dir(id) / "key"; }
        std::filesystem::path meta_file(const std::string &id) const { return link_dir(id) / "meta.json"; }
        std::filesystem::path inbox_dir() const { return root / "flowlink" / "inbox"; }
        std::filesystem::path identity_file() const { return root / "flowlink" / "identity.json"; }

        // ---- identity ------------------------------------------------------
        json identity(const std::string &fallback_name)
        {
            std::error_code ec;
            std::ifstream f(identity_file(), std::ios::binary);
            if (f)
            {
                json j = json::parse(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), nullptr, false);
                if (j.is_object() && j.contains("client_id") && j["client_id"].is_string() &&
                    is_hex_id(j["client_id"].get<std::string>(), 32))
                    return j;
            }
            json j{{"version", 1},
                   {"client_id", hex_encode(random_bytes(16))},
                   {"name", clean_name(fallback_name)},
                   {"created_at", (long long)std::time(nullptr)}};
            std::filesystem::create_directories(identity_file().parent_path(), ec);
            std::ofstream out(identity_file(), std::ios::trunc | std::ios::binary);
            if (out.is_open())
            {
                out << j.dump(2) << "\n";
                out.close();
            }
            return j;
        }
        bool save_identity_name(const std::string &name)
        {
            json j = identity(default_name());
            j["name"] = clean_name(name);
            std::ofstream out(identity_file(), std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << j.dump(2) << "\n";
            return out.good();
        }

        // ---- link key ------------------------------------------------------
        bool save_key(const std::string &id, const std::string &key)
        {
            return sealed_file::write(root, key_file(id), key);
        }
        bool load_key(const std::string &id, std::string &key)
        {
            return sealed_file::read(root, key_file(id), key);
        }

        // ---- peer metadata -------------------------------------------------
        bool save_meta(const peer_meta &m)
        {
            std::error_code ec;
            std::filesystem::create_directories(link_dir(m.id), ec);
            std::ofstream out(meta_file(m.id), std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << m.to_json().dump(2) << "\n";
            return out.good();
        }
        std::optional<peer_meta> load_meta(const std::string &id)
        {
            std::ifstream f(meta_file(id), std::ios::binary);
            if (!f)
                return std::nullopt;
            json j = json::parse(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), nullptr, false);
            if (!j.is_object())
                return std::nullopt;
            peer_meta m = peer_meta::from_json(j);
            if (m.id.empty())
                m.id = id;
            return m;
        }
        std::vector<peer_meta> all_meta()
        {
            std::vector<peer_meta> out;
            std::error_code ec;
            auto it = std::filesystem::directory_iterator(links(), ec);
            for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                if (!it->is_directory(ec))
                    continue;
                const std::string id = it->path().filename().string();
                if (!is_hex_id(id, 32))
                    continue;
                if (auto m = load_meta(id))
                    out.push_back(*m);
            }
            std::sort(out.begin(), out.end(), [](const peer_meta &a, const peer_meta &b)
                      { return a.name < b.name; });
            return out;
        }
        bool forget(const std::string &id)
        {
            std::error_code ec;
            if (!is_hex_id(id, 32))
                return false;
            std::filesystem::remove_all(link_dir(id), ec);
            return !ec;
        }

        // ---- mailbox -------------------------------------------------------
        bool store_mail(const mail_item &m)
        {
            if (m.id.empty())
                return false;
            std::error_code ec;
            std::filesystem::create_directories(inbox_dir(), ec);
            json j{{"id", m.id}, {"from", m.from}, {"from_name", m.from_name},
                   {"subject", m.subject}, {"note", m.note}, {"kind", m.kind},
                   {"created", m.created}, {"read", m.read}, {"payload", m.payload}};
            std::ofstream out(inbox_dir() / (m.id + ".json"), std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << j.dump() << "\n";
            return out.good();
        }
        std::vector<mail_item> list_mail()
        {
            std::vector<mail_item> out;
            std::error_code ec;
            auto it = std::filesystem::directory_iterator(inbox_dir(), ec);
            for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                    continue;
                std::ifstream f(it->path(), std::ios::binary);
                if (!f)
                    continue;
                json j = json::parse(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), nullptr, false);
                if (!j.is_object())
                    continue;
                mail_item m = mail_from_json(j);
                if (m.id.empty())
                    m.id = it->path().stem().string();
                out.push_back(std::move(m));
            }
            std::sort(out.begin(), out.end(), [](const mail_item &a, const mail_item &b)
                      { return a.created > b.created; });
            return out;
        }
        std::optional<mail_item> read_mail(const std::string &id)
        {
            if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos ||
                id.find("..") != std::string::npos)
                return std::nullopt;
            std::ifstream f(inbox_dir() / (id + ".json"), std::ios::binary);
            if (!f)
                return std::nullopt;
            json j = json::parse(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), nullptr, false);
            if (!j.is_object())
                return std::nullopt;
            return mail_from_json(j);
        }
        bool mark_read(const std::string &id, bool read)
        {
            auto m = read_mail(id);
            if (!m)
                return false;
            m->read = read;
            return store_mail(*m);
        }
        bool drop_mail(const std::string &id)
        {
            if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos)
                return false;
            std::error_code ec;
            return std::filesystem::remove(inbox_dir() / (id + ".json"), ec);
        }
        std::pair<int, int> mail_counts()
        {
            int unread = 0, total = 0;
            for (auto &m : list_mail())
            {
                total++;
                if (!m.read)
                    unread++;
            }
            return {unread, total};
        }
        static std::string new_mail_id()
        {
            return std::format("m-{}-{}", (long long)std::time(nullptr), hex_encode(random_bytes(4)));
        }
    };

    // =========================================================================
    //  service — the whole FlowLink stack behind one object. Constructing it
    //  allocates nothing but the impl; start() binds sockets and spawns the
    //  worker threads; stop() joins them. A disabled FlowLink is a null
    //  unique_ptr in app_core: no memory, no threads, no sockets.
    // =========================================================================
    struct impl : std::enable_shared_from_this<impl>
    {
        struct options
        {
            std::filesystem::path root;
            std::string name;
            std::uint16_t port = kDefaultPort;
            std::uint16_t discovery_port = kDefaultDiscoveryPort;
            bool announce = true;
            bool accept_remote = false;
            std::function<void(const json &)> on_event;
            std::function<void(const std::string &, const std::string &)> on_log;
            // methods this module does not implement itself (fs.*, tw.*): the
            // shell answers them, because only it knows the workspaces
            std::function<json(const std::string &, const json &, std::string &)> on_call;
        };

        // ---- connection ---------------------------------------------------
        struct pending_rpc
        {
            std::mutex mx;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            json result;
            std::string error;
            void finish(bool good, json r, std::string e)
            {
                {
                    std::lock_guard<std::mutex> lk(mx);
                    if (done)
                        return;
                    done = true;
                    ok = good;
                    result = std::move(r);
                    error = std::move(e);
                }
                cv.notify_all();
            }
        };

        struct conn : std::enable_shared_from_this<conn>
        {
            std::shared_ptr<impl> svc;
            tcp::socket sock;
            asio::strand<asio::io_context::executor_type> strand;
            bool inbound = false;

            // handshake state
            bool hello_sent = false;
            bool ready = false;
            std::string peer_id, peer_name;
            std::uint16_t peer_port = 0; // the port the peer LISTENS on (its source port is ephemeral)
            std::string my_eph_pub, my_eph_sec;
            std::string peer_eph_pub;
            std::string psk;        // pairing code PSK or link key
            bool pair_mode = false;
            std::string pairing_code; // client side: the code we typed
            std::string link_key;     // pair mode, initiator side: key we hand over
            std::string hs_tx, hs_rx; // handshake keys
            std::string app_tx, app_rx;

            // frame IO
            std::array<unsigned char, 4> hdr{};
            std::string body;
            std::deque<std::string> wq;
            bool writing = false;
            std::atomic<bool> closed{false};
            std::string close_reason;

            // rpc bookkeeping
            std::mutex rmx;
            std::map<std::uint64_t, std::shared_ptr<pending_rpc>> pend;
            std::uint64_t next_id = 1;

            // handshake wait
            std::mutex wmx;
            std::condition_variable wcv;
            int state = 0; // 0 connecting/handshaking, 1 ready, -1 failed
            std::string fail_reason;
            long long last_rx = 0;

            explicit conn(std::shared_ptr<impl> s, tcp::socket sock_)
                : svc(std::move(s)), sock(std::move(sock_)), strand(asio::make_strand(svc->io)) {}

            void start_inbound();
            void start_outbound(const std::string &mode, const std::string &code);
            void read_header();
            void read_body(std::uint32_t len);
            void on_frame(const std::string &text);
            void on_hello(const json &j);
            void on_hello_ack(const json &j);
            void on_confirm(const json &j);
            void on_enc(const json &j);
            void handshake_done();
            void fail(const std::string &why);

            void send_raw(std::string text);
            void send_encrypted(const json &payload);
            void do_write();
            void close(const std::string &why);

            bool request(const std::string &method, const json &params, int timeout_ms, json &out, std::string &err);
            bool wait_ready(int timeout_ms, std::string &err);
            void finish_wait(int st, const std::string &why);
            void kill_pending(const std::string &why);
        };

        options opt;
        store st;
        std::string client_id;
        std::string name;

        asio::io_context io;
        std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work;
        std::vector<std::thread> threads;
        std::unique_ptr<tcp::acceptor> acceptor;
        std::unique_ptr<udp::socket> udp_sock;
        asio::steady_timer announce_timer{io};
        asio::steady_timer sweep_timer{io};
        asio::steady_timer pair_timer{io};
        udp::endpoint group_endpoint;
        // every address an announce goes to: 255.255.255.255, the multicast
        // group, and the /24 broadcast of each local IPv4 (a host with a docker
        // bridge or two NICs does not route 255.255.255.255 to every subnet)
        std::vector<udp::endpoint> announce_targets;
        std::atomic<bool> alive{false};
        std::uint16_t bound_port = 0;

        mutable std::mutex mx; // guards peers / sessions / pairing / handlers
        struct peer_state
        {
            std::string id, name, addr;
            std::uint16_t port = 0;
            long long last_seen = 0;
            bool pairing_open = false;
        };
        std::map<std::string, peer_state> discovered;
        std::map<std::string, std::shared_ptr<conn>> sessions;
        std::map<std::string, std::function<json(const json &, std::string &)>> handlers;

        struct pairing_state
        {
            bool open = false;
            std::string code;     // normalized (no separators)
            std::string display;  // grouped, for the UI
            long long expires = 0;
            int fails = 0;
            std::string state = "idle"; // idle | waiting | paired | failed
            std::string peer;           // client_id once paired
        } pairing;

        explicit impl(options o) : opt(std::move(o)), st{opt.root} {}
        ~impl() { stop(); }

        // ---- lifecycle ----------------------------------------------------
        bool start(std::string &err);
        void stop();
        void log(const std::string &level, const std::string &msg)
        {
            if (opt.on_log)
                opt.on_log(level, msg);
        }
        void emit(json ev)
        {
            if (opt.on_event)
                opt.on_event(ev);
        }
        bool is_pairing_open()
        {
            std::lock_guard<std::mutex> lk(mx);
            return pairing.open && (long long)std::time(nullptr) < pairing.expires;
        }

        // ---- discovery ----------------------------------------------------
        void build_announce_targets();
        void announce_loop();
        void announce_once(bool probe_reply, const udp::endpoint *to);
        void start_receive();
        void on_datagram(const std::string &text, const udp::endpoint &from);
        void probe_now();

        // ---- inbound connections -----------------------------------------
        void accept_loop();
        std::shared_ptr<conn> make_conn(tcp::socket s);

        // ---- sessions -----------------------------------------------------
        std::shared_ptr<conn> session_for(const std::string &peer, int timeout_ms, std::string &err);
        void register_session(const std::shared_ptr<conn> &c);
        void drop_session(const std::string &peer, const std::shared_ptr<conn> &c);
        void sweep_loop();
        void pair_expire_loop();

        // ---- handlers -----------------------------------------------------
        json dispatch(const std::string &method, const json &params, std::string &err);
        json deliver_mail(const json &params, std::string &err);

        // ---- views --------------------------------------------------------
        json status();
        json peers_json();
    };

    // -------------------------------------------------------------------------
    //  conn — handshake
    // -------------------------------------------------------------------------
    inline void impl::conn::start_inbound()
    {
        auto self = shared_from_this();
        sock.set_option(tcp::no_delay(true));
        last_rx = (long long)std::time(nullptr);
        read_header();
    }

    inline void impl::conn::start_outbound(const std::string &mode, const std::string &code)
    {
        auto self = shared_from_this();
        sock.set_option(tcp::no_delay(true));
        last_rx = (long long)std::time(nullptr);
        pair_mode = (mode == "pair");
        pairing_code = code;
        x25519_pair k = x25519_new();
        my_eph_pub = k.pub;
        my_eph_sec = k.sec;
        json hello{{"t", "hello"},
                   {"m", kProto},
                   {"v", kVersion},
                   {"id", svc->client_id},
                   {"name", svc->name},
                   {"eph", b64_encode(my_eph_pub)},
                   {"port", svc->bound_port},
                   {"mode", pair_mode ? "pair" : "resume"},
                   {"ts", (long long)std::time(nullptr)}};
        hello_sent = true;
        send_raw(hello.dump());
        read_header();
    }

    inline void impl::conn::read_header()
    {
        if (closed.load())
            return;
        auto self = shared_from_this();
        asio::async_read(sock, asio::buffer(hdr),
                         asio::bind_executor(strand, [self](const boost::system::error_code &ec, std::size_t)
                                             {
                if (ec) { self->close("read failed"); return; }
                const std::uint32_t len = ((std::uint32_t)self->hdr[0] << 24) | ((std::uint32_t)self->hdr[1] << 16) |
                                          ((std::uint32_t)self->hdr[2] << 8) | (std::uint32_t)self->hdr[3];
                if (len == 0 || len > kMaxFrame) { self->close("frame too large"); return; }
                self->read_body(len); }));
    }

    inline void impl::conn::read_body(std::uint32_t len)
    {
        auto self = shared_from_this();
        auto buf = std::make_shared<std::string>(len, '\0');
        asio::async_read(sock, asio::buffer(*buf),
                         asio::bind_executor(strand, [self, buf](const boost::system::error_code &ec, std::size_t)
                                             {
                if (ec) { self->close("read failed"); return; }
                self->last_rx = (long long)std::time(nullptr);
                self->on_frame(*buf);
                if (!self->closed.load()) self->read_header(); }));
    }

    inline void impl::conn::send_raw(std::string text)
    {
        if (closed.load() || text.size() > kMaxFrame)
            return;
        std::string frame(4, '\0');
        const std::uint32_t len = (std::uint32_t)text.size();
        frame[0] = (char)((len >> 24) & 0xff);
        frame[1] = (char)((len >> 16) & 0xff);
        frame[2] = (char)((len >> 8) & 0xff);
        frame[3] = (char)(len & 0xff);
        frame += text;
        const bool idle = wq.empty();
        wq.push_back(std::move(frame));
        if (idle && !writing)
        {
            writing = true;
            do_write();
        }
    }

    inline void impl::conn::do_write()
    {
        if (closed.load() || wq.empty())
        {
            writing = false;
            return;
        }
        auto self = shared_from_this();
        asio::async_write(sock, asio::buffer(wq.front()),
                          asio::bind_executor(strand, [self](const boost::system::error_code &ec, std::size_t)
                                              {
                if (ec) { self->close("write failed"); return; }
                self->wq.pop_front();
                if (self->wq.empty()) { self->writing = false; return; }
                self->do_write(); }));
    }

    inline void impl::conn::send_encrypted(const json &payload)
    {
        if (!ready || closed.load())
            return;
        json frame{{"t", "enc"}, {"b", aead_seal(app_tx, "fl-data", payload.dump())}};
        send_raw(frame.dump());
    }

    inline void impl::conn::close(const std::string &why)
    {
        if (closed.exchange(true))
            return;
        close_reason = why;
        boost::system::error_code ec;
        sock.shutdown(tcp::socket::shutdown_both, ec);
        sock.close(ec);
        finish_wait(-1, why);
        kill_pending(why);
        auto svc_ = svc;
        const std::string pid = peer_id;
        asio::post(svc->io, [svc_, pid, self = shared_from_this()]
                   { svc_->drop_session(pid, self); });
    }

    inline void impl::conn::finish_wait(int st, const std::string &why)
    {
        {
            std::lock_guard<std::mutex> lk(wmx);
            if (state != 0)
                return;
            state = st;
            if (st < 0)
                fail_reason = why;
        }
        wcv.notify_all();
    }

    inline void impl::conn::kill_pending(const std::string &why)
    {
        std::map<std::uint64_t, std::shared_ptr<pending_rpc>> take;
        {
            std::lock_guard<std::mutex> lk(rmx);
            take.swap(pend);
        }
        for (auto &[id, p] : take)
            p->finish(false, json{}, why);
    }

    inline bool impl::conn::wait_ready(int timeout_ms, std::string &err)
    {
        std::unique_lock<std::mutex> lk(wmx);
        if (!wcv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]
                          { return state != 0; }))
        {
            err = "handshake timed out";
            return false;
        }
        if (state < 0)
        {
            err = fail_reason.empty() ? "handshake failed" : fail_reason;
            return false;
        }
        return true;
    }

    inline void impl::conn::fail(const std::string &why)
    {
        json err{{"t", "error"}, {"msg", why}};
        send_raw(err.dump());
        finish_wait(-1, why);
        close(why);
    }

    // The transcript hash both sides must agree on. Built from an explicit
    // ordered field list (not from JSON text) so key order can never matter.
    // Ports are in initiator/responder order so both sides derive the same
    // string, and they are covered by the MAC: a forged port would otherwise
    // poison the address remembered for reconnects.
    static inline std::string transcript_hash(const std::string &a_id, const std::string &b_id,
                                              const std::string &a_eph, const std::string &b_eph,
                                              const std::string &mode,
                                              std::uint16_t a_port, std::uint16_t b_port)
    {
        return sha256("fl1|" + a_id + "|" + b_id + "|" + a_eph + "|" + b_eph + "|" + mode +
                      "|" + std::to_string(a_port) + "|" + std::to_string(b_port));
    }

    inline void impl::conn::on_hello(const json &j)
    {
        if (ready)
            return;
        const std::string their_id = j.value("id", "");
        if (!is_hex_id(their_id, 32))
            return fail("bad client id");
        if (their_id == svc->client_id)
            return fail("refusing a connection from ourselves");
        const std::string mode = j.value("mode", "resume");
        peer_id = their_id;
        peer_name = clean_name(j.value("name", ""));
        peer_port = (std::uint16_t)std::clamp(j.value("port", 0), 0, 65535);
        if (!b64_decode(j.value("eph", ""), peer_eph_pub) ||
            peer_eph_pub.size() != crypto_kx_PUBLICKEYBYTES)
            return fail("bad ephemeral key");

        std::string link;
        if (mode == "pair")
        {
            pair_mode = true;
            std::string code;
            {
                std::lock_guard<std::mutex> lk(svc->mx);
                if (!svc->pairing.open || (long long)std::time(nullptr) >= svc->pairing.expires)
                    return fail("pairing is not open on this cell");
                code = svc->pairing.code;
            }
            psk = argon2id(code, pair_salt(svc->client_id, their_id));
            if (psk.empty())
                return fail("key derivation failed");
        }
        else
        {
            if (!svc->st.load_key(their_id, link) || link.size() != kKeyBytes)
                return fail("unknown peer: pair first");
            psk = link;
        }

        x25519_pair k = x25519_new();
        my_eph_pub = k.pub;
        my_eph_sec = k.sec;
        std::string dh;
        if (!x25519_shared(my_eph_sec, peer_eph_pub, dh))
            return fail("bad ephemeral key");
        const std::string prk = hkdf_extract(psk, dh);
        const std::string th = transcript_hash(their_id, svc->client_id, peer_eph_pub, my_eph_pub, mode,
                                               peer_port, svc->bound_port);
        const std::string mac_key = hkdf_expand(prk, "fl hs mac", 32);
        const std::string mac = hmac_sha256(mac_key, th);

        json ack{{"t", "hello_ack"},
                 {"m", kProto},
                 {"v", kVersion},
                 {"id", svc->client_id},
                 {"name", svc->name},
                 {"eph", b64_encode(my_eph_pub)},
                 {"port", svc->bound_port},
                 {"mode", mode},
                 {"mac", b64_encode(mac)}};
        send_raw(ack.dump());

        hs_tx = hkdf_expand(prk, "fl hs s2c", 32);
        hs_rx = hkdf_expand(prk, "fl hs c2s", 32);
        app_tx = hkdf_expand(prk, "fl app s2c", 32);
        app_rx = hkdf_expand(prk, "fl app c2s", 32);
        // our CONFIRM proves we derived the same secret; the initiator answers
        // with its own (and, when pairing, the link key)
        json confirm{{"t", "confirm"}, {"th", hex_encode(th)}};
        json sealed{{"t", "enc"}, {"b", aead_seal(hs_tx, "fl-hs", confirm.dump())}};
        send_raw(sealed.dump());
        hello_sent = true;
        (void)link;
    }

    inline void impl::conn::on_hello_ack(const json &j)
    {
        if (!hello_sent || ready)
            return;
        const std::string their_id = j.value("id", "");
        if (!is_hex_id(their_id, 32))
            return fail("bad client id");
        peer_id = their_id;
        peer_name = clean_name(j.value("name", ""));
        peer_port = (std::uint16_t)std::clamp(j.value("port", 0), 0, 65535);
        if (!b64_decode(j.value("eph", ""), peer_eph_pub) || peer_eph_pub.size() != crypto_kx_PUBLICKEYBYTES)
            return fail("bad ephemeral key");

        std::string psk_local;
        if (pair_mode)
        {
            if (pairing_code.empty())
                return fail("no pairing code");
            psk_local = argon2id(pairing_code, pair_salt(svc->client_id, their_id));
            if (psk_local.empty())
                return fail("key derivation failed");
        }
        else
        {
            if (!svc->st.load_key(their_id, psk_local) || psk_local.size() != kKeyBytes)
                return fail("unknown peer: pair first");
        }
        psk = psk_local;

        std::string dh;
        if (!x25519_shared(my_eph_sec, peer_eph_pub, dh))
            return fail("bad ephemeral key");
        const std::string prk = hkdf_extract(psk, dh);
        const std::string mode = pair_mode ? "pair" : "resume";
        const std::string th = transcript_hash(svc->client_id, their_id, my_eph_pub, peer_eph_pub, mode,
                                               svc->bound_port, peer_port);
        const std::string mac_key = hkdf_expand(prk, "fl hs mac", 32);
        std::string their_mac;
        if (!b64_decode(j.value("mac", ""), their_mac) || !const_eq(their_mac, hmac_sha256(mac_key, th)))
            return fail("pairing code mismatch");

        hs_tx = hkdf_expand(prk, "fl hs c2s", 32);
        hs_rx = hkdf_expand(prk, "fl hs s2c", 32);
        app_tx = hkdf_expand(prk, "fl app c2s", 32);
        app_rx = hkdf_expand(prk, "fl app s2c", 32);

        json confirm{{"t", "confirm"}, {"th", hex_encode(th)}};
        if (pair_mode)
        {
            link_key = random_bytes(kKeyBytes);
            confirm["link"] = b64_encode(link_key);
        }
        json sealed{{"t", "enc"}, {"b", aead_seal(hs_tx, "fl-hs", confirm.dump())}};
        send_raw(sealed.dump());
    }

    inline void impl::conn::on_confirm(const json &j)
    {
        const std::string mode = pair_mode ? "pair" : "resume";
        const std::string expect = hex_encode(
            inbound ? transcript_hash(peer_id, svc->client_id, peer_eph_pub, my_eph_pub, mode,
                                      peer_port, svc->bound_port)
                    : transcript_hash(svc->client_id, peer_id, my_eph_pub, peer_eph_pub, mode,
                                      svc->bound_port, peer_port));
        if (j.value("th", "") != expect)
            return fail("handshake confirmation mismatch");

        if (inbound && pair_mode)
        {
            // the initiator hands over the long-term key inside the encrypted
            // handshake; from now on this pair reconnects without a code
            std::string link;
            if (!b64_decode(j.value("link", ""), link) || link.size() != kKeyBytes)
                return fail("pairing did not deliver a link key");
            if (!svc->st.save_key(peer_id, link))
                return fail("could not store the link key");
            peer_meta m;
            m.id = peer_id;
            m.name = peer_name.empty() ? peer_id.substr(0, 8) : peer_name;
            boost::system::error_code rec;
            const tcp::endpoint remote = sock.remote_endpoint(rec);
            if (!rec)
            {
                m.addr = remote.address().to_string();
                // the LISTENING port, not the ephemeral source port of this
                // connection: the peer must be reachable in the other direction
                m.port = peer_port ? peer_port : remote.port();
            }
            m.paired_at = (long long)std::time(nullptr);
            m.last_seen = m.paired_at;
            m.via = "code";
            m.paired = true;
            svc->st.save_meta(m);
            svc->log("info", std::format("paired with {} ({})", m.name, peer_id));
            {
                std::lock_guard<std::mutex> lk(svc->mx);
                svc->pairing.state = "paired";
                svc->pairing.peer = peer_id;
                svc->pairing.open = false;
                svc->pairing.code.clear();
            }
            svc->emit({{"type", "paired"}, {"client_id", peer_id}, {"name", m.name}});
        }
        if (!inbound && pair_mode)
        {
            if (!svc->st.save_key(peer_id, link_key))
                return fail("could not store the link key");
            peer_meta m;
            m.id = peer_id;
            m.name = peer_name.empty() ? peer_id.substr(0, 8) : peer_name;
            boost::system::error_code rec;
            const tcp::endpoint remote = sock.remote_endpoint(rec);
            if (!rec)
            {
                m.addr = remote.address().to_string();
                // the LISTENING port, not the ephemeral source port of this
                // connection: the peer must be reachable in the other direction
                m.port = peer_port ? peer_port : remote.port();
            }
            m.paired_at = (long long)std::time(nullptr);
            m.last_seen = m.paired_at;
            m.via = "entered";
            m.paired = true;
            svc->st.save_meta(m);
            svc->log("info", std::format("paired with {} ({})", m.name, peer_id));
            svc->emit({{"type", "paired"}, {"client_id", peer_id}, {"name", m.name}});
        }
        handshake_done();
    }

    inline void impl::conn::handshake_done()
    {
        if (ready)
            return;
        ready = true;
        // refresh the peer's last-known address so a reconnect needs no discovery
        if (auto m = svc->st.load_meta(peer_id))
        {
            boost::system::error_code ec;
            const tcp::endpoint remote = sock.remote_endpoint(ec);
            if (!ec)
                m->addr = remote.address().to_string();
            m->port = peer_port ? peer_port : remote.port();
            m->last_seen = (long long)std::time(nullptr);
            m->paired = true;
            if (!m->name.empty() && peer_name.empty())
                peer_name = m->name;
            svc->st.save_meta(*m);
        }
        svc->register_session(shared_from_this());
        finish_wait(1, {});
        svc->emit({{"type", "online"}, {"client_id", peer_id}, {"name", peer_name}});
        send_encrypted(json{{"t", "hello"}, {"name", svc->name}, {"client_id", svc->client_id},
                            {"port", svc->bound_port},
                            {"accept_remote", svc->opt.accept_remote}});
    }

    inline void impl::conn::on_frame(const std::string &text)
    {
        json j = json::parse(text, nullptr, false);
        if (!j.is_object())
            return close("malformed frame");
        const std::string t = j.value("t", "");
        if (t == "hello" && inbound && !ready)
            return on_hello(j);
        if (t == "hello_ack" && !inbound && !ready)
            return on_hello_ack(j);
        if (t == "error")
            return fail(j.value("msg", "peer error"));
        if (t == "enc")
        {
            std::string plain;
            // CONFIRM rides the handshake keys, everything after it the app keys
            if (!ready)
            {
                if (!aead_open(hs_rx, "fl-hs", j.value("b", json::object()), plain))
                    return fail("handshake frame did not decrypt");
                json c = json::parse(plain, nullptr, false);
                if (!c.is_object() || c.value("t", "") != "confirm")
                    return fail("unexpected handshake frame");
                return on_confirm(c);
            }
            if (!aead_open(app_rx, "fl-data", j.value("b", json::object()), plain))
                return close("frame did not decrypt");
            json p = json::parse(plain, nullptr, false);
            if (!p.is_object())
                return close("malformed payload");
            const std::string pt = p.value("t", "");
            if (pt == "hello")
            {
                peer_name = clean_name(p.value("name", peer_name));
                const int port = p.value("port", 0);
                if (port > 0 && port <= 65535)
                    peer_port = (std::uint16_t)port;
                return;
            }
            if (pt == "ping")
            {
                send_encrypted(json{{"t", "pong"}});
                return;
            }
            if (pt == "pong")
                return;
            if (pt == "req")
            {
                const std::uint64_t id = p.value("id", (std::uint64_t)0);
                const std::string method = p.value("m", "");
                const json params = p.contains("p") ? p["p"] : json::object();
                auto self = shared_from_this();
                // handlers may block for minutes (a remote teamwork run): run
                // them off the io thread, answer back on the strand
                std::thread([self, id, method, params]
                            {
                    std::string err;
                    json r;
                    bool ok = true;
                    try { r = self->svc->dispatch(method, params, err); }
                    catch (const std::exception &e) { ok = false; err = e.what(); }
                    if (!err.empty()) ok = false;
                    json res{{"t", "res"}, {"id", id}, {"ok", ok}};
                    if (ok) res["r"] = r; else res["e"] = err;
                    auto c = self;
                    asio::post(c->strand, [c, res] { c->send_encrypted(res); }); })
                    .detach();
                return;
            }
            if (pt == "res")
            {
                const std::uint64_t id = p.value("id", (std::uint64_t)0);
                std::shared_ptr<pending_rpc> pd;
                {
                    std::lock_guard<std::mutex> lk(rmx);
                    auto it = pend.find(id);
                    if (it == pend.end())
                        return;
                    pd = it->second;
                    pend.erase(it);
                }
                const bool ok = p.value("ok", false);
                pd->finish(ok, p.contains("r") ? p["r"] : json{}, p.value("e", std::string{}));
                return;
            }
            if (pt == "evt")
            {
                json ev{{"type", "peer_event"}, {"client_id", peer_id},
                        {"method", p.value("m", "")}, {"params", p.contains("p") ? p["p"] : json{}}};
                svc->emit(ev);
                return;
            }
            return;
        }
        if (t == "ping")
            return send_raw(json{{"t", "pong"}}.dump());
        if (t == "pong")
            return;
        close("unexpected frame");
    }

    inline bool impl::conn::request(const std::string &method, const json &params, int timeout_ms,
                                    json &out, std::string &err)
    {
        if (!ready || closed.load())
        {
            err = "not connected";
            return false;
        }
        auto pd = std::make_shared<pending_rpc>();
        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lk(rmx);
            id = next_id++;
            pend[id] = pd;
        }
        send_encrypted(json{{"t", "req"}, {"id", id}, {"m", method}, {"p", params}});
        std::unique_lock<std::mutex> lk(pd->mx);
        if (!pd->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]
                             { return pd->done; }))
        {
            std::lock_guard<std::mutex> g(rmx);
            pend.erase(id);
            err = "request timed out";
            return false;
        }
        if (!pd->ok)
        {
            err = pd->error.empty() ? "peer refused the request" : pd->error;
            return false;
        }
        out = pd->result;
        return true;
    }

    // -------------------------------------------------------------------------
    //  impl — lifecycle
    // -------------------------------------------------------------------------
    inline bool impl::start(std::string &err)
    {
        if (alive.load())
        {
            err = "already running";
            return false;
        }
        if (sodium_init() < 0)
        {
            err = "libsodium initialisation failed";
            return false;
        }
        const json ident = st.identity(opt.name.empty() ? default_name() : opt.name);
        client_id = ident.value("client_id", "");
        name = clean_name(opt.name.empty() ? ident.value("name", default_name()) : opt.name);
        if (!is_hex_id(client_id, 32))
        {
            err = "identity file is corrupt (client_id)";
            return false;
        }
        std::error_code fec;
        std::filesystem::create_directories(st.links(), fec);
        std::filesystem::create_directories(st.inbox_dir(), fec);
        boost::system::error_code ec;

        // ---- inbound TCP ------------------------------------------------
        try
        {
            acceptor = std::make_unique<tcp::acceptor>(io);
            acceptor->open(tcp::v4(), ec);
            if (ec)
            {
                err = "could not open the transport socket: " + ec.message();
                return false;
            }
#ifndef _WIN32
            // SO_REUSEADDR on a Windows TCP listener lets another process bind
            // the same port and take the connections over — exactly the wrong
            // property for a service that carries keys. POSIX needs it only to
            // survive TIME_WAIT after a restart.
            acceptor->set_option(tcp::acceptor::reuse_address(true), ec);
#endif
            acceptor->bind(tcp::endpoint(tcp::v4(), opt.port), ec);
            if (ec)
            {
                err = std::format("could not bind TCP port {}: {}", opt.port, ec.message());
                acceptor.reset();
                return false;
            }
            acceptor->listen(asio::socket_base::max_listen_connections, ec);
            if (ec)
            {
                err = "could not listen: " + ec.message();
                acceptor.reset();
                return false;
            }
            bound_port = acceptor->local_endpoint(ec).port();
        }
        catch (const std::exception &e)
        {
            err = std::string("transport setup failed: ") + e.what();
            return false;
        }

        // ---- UDP discovery ----------------------------------------------
        if (opt.announce)
        {
            try
            {
                udp_sock = std::make_unique<udp::socket>(io);
                udp_sock->open(udp::v4(), ec);
                if (!ec)
                {
                    udp_sock->set_option(asio::socket_base::reuse_address(true), ec);
                    udp_sock->set_option(asio::socket_base::broadcast(true), ec);
                    udp_sock->bind(udp::endpoint(udp::v4(), opt.discovery_port), ec);
                    if (ec)
                    {
                        // another instance on this host may already hold the
                        // port: keep the transport, drop discovery
                        log("warn", std::format("discovery disabled: bind {} failed: {}",
                                                opt.discovery_port, ec.message()));
                        udp_sock.reset();
                    }
                    else
                    {
                        boost::system::error_code jec;
                        udp_sock->set_option(asio::ip::multicast::join_group(
                                                 asio::ip::make_address_v4(kGroup)),
                                             jec);
                        udp_sock->set_option(asio::ip::multicast::enable_loopback(true), jec);
                        group_endpoint = udp::endpoint(asio::ip::make_address_v4(kGroup), opt.discovery_port);
                        build_announce_targets();
                    }
                }
                else
                {
                    udp_sock.reset();
                }
            }
            catch (const std::exception &e)
            {
                udp_sock.reset();
                log("warn", std::string("discovery disabled: ") + e.what());
            }
        }

        alive.store(true);
        work.emplace(asio::make_work_guard(io));
        accept_loop();
        if (udp_sock)
        {
            start_receive();
            announce_loop();
        }
        sweep_loop();
        pair_expire_loop();
        const unsigned n = std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2));
        for (unsigned i = 0; i < n; i++)
            threads.emplace_back([this]
                                 {
                try { io.run(); } catch (const std::exception &) {} });
        log("info", std::format("flowlink started: id={} port={} discovery={} name={}",
                                client_id, bound_port, udp_sock ? opt.discovery_port : 0, name));
        return true;
    }

    inline void impl::stop()
    {
        if (!alive.exchange(false))
            return;
        boost::system::error_code ec;
        if (acceptor)
        {
            acceptor->close(ec);
            acceptor.reset();
        }
        if (udp_sock)
        {
            udp_sock->close(ec);
            udp_sock.reset();
        }
        announce_timer.cancel();
        sweep_timer.cancel();
        pair_timer.cancel();
        std::map<std::string, std::shared_ptr<conn>> drop;
        {
            std::lock_guard<std::mutex> lk(mx);
            drop.swap(sessions);
            discovered.clear();
            pairing = pairing_state{};
        }
        for (auto &[id, c] : drop)
            c->close("service stopping");
        work.reset();
        io.stop();
        for (auto &t : threads)
            if (t.joinable())
                t.join();
        threads.clear();
        io.restart();
        log("info", "flowlink stopped");
    }

    // -------------------------------------------------------------------------
    //  impl — discovery
    // -------------------------------------------------------------------------
    inline void impl::build_announce_targets()
    {
        announce_targets.clear();
        announce_targets.emplace_back(asio::ip::address_v4::broadcast(), opt.discovery_port);
        if (group_endpoint.port() != 0)
            announce_targets.push_back(group_endpoint);
        // Every local IPv4 subnet gets its own broadcast address: a host with a
        // docker bridge, a VPN or two NICs does not route 255.255.255.255 to all
        // of them. Resolving the host name is portable and needs no platform
        // adapter enumeration; the /24 guess is what makes it useful.
        boost::system::error_code ec;
        tcp::resolver res(io);
        auto results = res.resolve(asio::ip::host_name(ec), "", ec);
        if (ec)
            return;
        std::set<std::string> seen;
        for (const auto &entry : results)
        {
            const auto addr = entry.endpoint().address();
            if (!addr.is_v4())
                continue;
            auto bytes = addr.to_v4().to_bytes();
            if (bytes[0] == 127)                              // loopback
                continue;
            if (bytes[0] == 169 && bytes[1] == 254)           // link-local
                continue;
            if (!seen.insert(addr.to_string()).second)
                continue;
            bytes[3] = 255;
            announce_targets.emplace_back(asio::ip::address_v4(bytes), opt.discovery_port);
        }
    }

    inline void impl::announce_once(bool probe_reply, const udp::endpoint *to)
    {
        if (!udp_sock || !alive.load())
            return;
        json p{{"m", kProto},
               {"v", kVersion},
               {"t", "announce"},
               {"id", client_id},
               {"name", name},
               {"port", bound_port},
               {"pair", is_pairing_open()},
               {"ts", (long long)std::time(nullptr)}};
        const std::string text = p.dump();
        boost::system::error_code ec;
        if (to)
        {
            udp_sock->send_to(asio::buffer(text), *to, 0, ec);
            return;
        }
        for (const auto &target : announce_targets)
            udp_sock->send_to(asio::buffer(text), target, 0, ec);
        (void)probe_reply;
    }

    inline void impl::announce_loop()
    {
        if (!alive.load())
            return;
        auto self = shared_from_this();
        const int jitter = (int)(randombytes_uniform(1200));
        announce_timer.expires_after(std::chrono::milliseconds(kAnnounceMillis + jitter));
        announce_timer.async_wait([self](const boost::system::error_code &ec)
                                  {
            if (ec || !self->alive.load()) return;
            self->announce_once(false, nullptr);
            self->announce_loop(); });
    }

    inline void impl::start_receive()
    {
        if (!udp_sock || !alive.load())
            return;
        auto self = shared_from_this();
        auto buf = std::make_shared<std::string>(kMaxDatagram, '\0');
        auto from = std::make_shared<udp::endpoint>();
        udp_sock->async_receive_from(asio::buffer(*buf), *from,
                                     [self, buf, from](const boost::system::error_code &ec, std::size_t n)
                                     {
                                         if (ec || !self->alive.load())
                                         {
                                             if (self->alive.load() && ec != asio::error::operation_aborted)
                                                 self->start_receive();
                                             return;
                                         }
                                         buf->resize(n);
                                         self->on_datagram(*buf, *from);
                                         self->start_receive();
                                     });
    }

    inline void impl::on_datagram(const std::string &text, const udp::endpoint &from)
    {
        json j = json::parse(text, nullptr, false);
        if (!j.is_object() || j.value("m", "") != kProto)
            return;
        if (j.value("v", 0) != kVersion)
            return;
        const std::string t = j.value("t", "");
        const std::string their_id = j.value("id", "");
        if (!is_hex_id(their_id, 32) || their_id == client_id)
            return;
        const std::string their_name = clean_name(j.value("name", ""));
        const std::uint16_t port = (std::uint16_t)std::clamp(j.value("port", 0), 0, 65535);
        if (port == 0)
            return;
        if (t == "probe")
        {
            announce_once(true, &from);
            return;
        }
        if (t != "announce")
            return;
        const bool pair_open = j.value("pair", false);
        {
            std::lock_guard<std::mutex> lk(mx);
            peer_state &p = discovered[their_id];
            const bool fresh = p.last_seen == 0;
            p.id = their_id;
            p.name = their_name;
            p.addr = from.address().to_string();
            p.port = port;
            p.last_seen = (long long)std::time(nullptr);
            p.pairing_open = pair_open;
            if (!fresh)
                return; // already known: refresh only, no event storm
        }
        emit({{"type", "discovered"}, {"client_id", their_id}, {"name", their_name},
              {"addr", from.address().to_string()}, {"port", port}, {"pairing", pair_open}});
    }

    inline void impl::probe_now()
    {
        if (!udp_sock || !alive.load())
            return;
        json p{{"m", kProto}, {"v", kVersion}, {"t", "probe"}, {"id", client_id},
               {"name", name}, {"port", bound_port}, {"ts", (long long)std::time(nullptr)}};
        const std::string text = p.dump();
        boost::system::error_code ec;
        for (const auto &target : announce_targets)
            udp_sock->send_to(asio::buffer(text), target, 0, ec);
    }

    // -------------------------------------------------------------------------
    //  impl — inbound connections and sessions
    // -------------------------------------------------------------------------
    inline std::shared_ptr<impl::conn> impl::make_conn(tcp::socket s)
    {
        return std::make_shared<conn>(shared_from_this(), std::move(s));
    }

    inline void impl::accept_loop()
    {
        if (!acceptor || !alive.load())
            return;
        auto self = shared_from_this();
        auto s = std::make_shared<tcp::socket>(io);
        acceptor->async_accept(*s, [self, s](const boost::system::error_code &ec)
                               {
            if (!self->alive.load()) return;
            if (!ec)
            {
                auto c = self->make_conn(std::move(*s));
                c->inbound = true;
                c->start_inbound();
            }
            self->accept_loop(); });
    }

    inline void impl::register_session(const std::shared_ptr<conn> &c)
    {
        std::shared_ptr<conn> old;
        {
            std::lock_guard<std::mutex> lk(mx);
            auto it = sessions.find(c->peer_id);
            if (it != sessions.end() && it->second != c)
                old = it->second;
            sessions[c->peer_id] = c;
        }
        if (old)
            old->close("superseded by a new session");
    }

    inline void impl::drop_session(const std::string &peer, const std::shared_ptr<conn> &c)
    {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lk(mx);
            auto it = sessions.find(peer);
            if (it != sessions.end() && it->second == c)
            {
                sessions.erase(it);
                removed = true;
            }
        }
        if (removed)
            emit({{"type", "offline"}, {"client_id", peer}});
    }

    inline std::shared_ptr<impl::conn> impl::session_for(const std::string &peer, int timeout_ms, std::string &err)
    {
        {
            std::lock_guard<std::mutex> lk(mx);
            auto it = sessions.find(peer);
            if (it != sessions.end())
            {
                // a live session, or one another caller is already handshaking
                // with: wait on the same attempt instead of opening a second one
                if (it->second->ready)
                    return it->second;
                auto joining = it->second;
                std::string why;
                if (joining->wait_ready(timeout_ms, why))
                    return joining;
                err = why;
                return nullptr;
            }
        }
        // where is it? discovery first, then the address remembered at pairing
        std::string addr;
        std::uint16_t port = 0;
        {
            std::lock_guard<std::mutex> lk(mx);
            if (auto it = discovered.find(peer); it != discovered.end())
            {
                addr = it->second.addr;
                port = it->second.port;
            }
        }
        if (addr.empty() || port == 0)
        {
            if (auto m = st.load_meta(peer))
            {
                addr = m->addr;
                port = m->port;
            }
        }
        if (addr.empty() || port == 0)
        {
            err = "peer address unknown: it is not on the network right now";
            return nullptr;
        }
        auto c = make_conn(tcp::socket(io));
        c->inbound = false;
        {
            std::lock_guard<std::mutex> lk(mx);
            sessions[peer] = c; // provisional: concurrent callers join this attempt
        }
        boost::system::error_code ec;
        const auto address = asio::ip::make_address(addr, ec);
        if (ec)
        {
            drop_session(peer, c);
            err = "bad peer address: " + addr;
            return nullptr;
        }
        const tcp::endpoint ep(address, port);
        auto self = shared_from_this();
        c->sock.async_connect(ep, asio::bind_executor(c->strand, [c](const boost::system::error_code &e)
                                                     {
            if (e) { c->fail("connect failed: " + e.message()); return; }
            c->start_outbound("resume", ""); }));
        std::string why;
        if (!c->wait_ready(timeout_ms, why))
        {
            drop_session(peer, c);
            c->close("handshake failed");
            err = why;
            return nullptr;
        }
        return c;
    }

    inline void impl::sweep_loop()
    {
        if (!alive.load())
            return;
        auto self = shared_from_this();
        sweep_timer.expires_after(std::chrono::seconds(10));
        sweep_timer.async_wait([self](const boost::system::error_code &ec)
                               {
            if (ec || !self->alive.load()) return;
            const long long now = (long long)std::time(nullptr);
            std::vector<std::shared_ptr<impl::conn>> idle;
            {
                std::lock_guard<std::mutex> lk(self->mx);
                for (auto it = self->discovered.begin(); it != self->discovered.end();)
                    if (now - it->second.last_seen > kPeerTtlSeconds)
                        it = self->discovered.erase(it);
                    else
                        ++it;
                for (auto &[id, c] : self->sessions)
                    if (c->ready && now - c->last_rx > kSessionIdleSeconds)
                        idle.push_back(c);
            }
            for (auto &c : idle)
                c->close("idle timeout");
            self->sweep_loop(); });
    }

    inline void impl::pair_expire_loop()
    {
        if (!alive.load())
            return;
        auto self = shared_from_this();
        pair_timer.expires_after(std::chrono::seconds(2));
        pair_timer.async_wait([self](const boost::system::error_code &ec)
                              {
            if (ec || !self->alive.load()) return;
            {
                std::lock_guard<std::mutex> lk(self->mx);
                if (self->pairing.open && (long long)std::time(nullptr) >= self->pairing.expires)
                {
                    self->pairing.open = false;
                    self->pairing.code.clear();
                    if (self->pairing.state == "waiting")
                        self->pairing.state = "expired";
                    self->emit({{"type", "pairing_expired"}});
                }
            }
            self->pair_expire_loop(); });
    }

    // -------------------------------------------------------------------------
    //  impl — rpc dispatch
    // -------------------------------------------------------------------------
    inline json impl::dispatch(const std::string &method, const json &params, std::string &err)
    {
        std::function<json(const json &, std::string &)> h;
        {
            std::lock_guard<std::mutex> lk(mx);
            auto it = handlers.find(method);
            if (it != handlers.end())
                h = it->second;
        }
        if (h)
        {
            json r = h(params, err);
            return r;
        }
        if (method == "mail.deliver")
            return deliver_mail(params, err);
        if (method == "ping")
            return json{{"ok", true}, {"ts", (long long)std::time(nullptr)}, {"name", name},
                        {"client_id", client_id}};
        if (opt.on_call)
            return opt.on_call(method, params, err);
        err = "unknown method: " + method;
        return json{};
    }

    inline json impl::deliver_mail(const json &params, std::string &err)
    {
        mail_item m = mail_from_json(params);
        if (m.id.empty())
            m.id = store::new_mail_id();
        if (m.from.empty())
        {
            err = "mail without a sender";
            return json{};
        }
        if (m.created == 0)
            m.created = (long long)std::time(nullptr);
        if (m.subject.empty())
            m.subject = "(no subject)";
        if (!st.store_mail(m))
        {
            err = "could not store the message";
            return json{};
        }
        emit({{"type", "mail"},
              {"id", m.id},
              {"from", m.from},
              {"from_name", m.from_name},
              {"subject", m.subject},
              {"kind", m.kind}});
        log("info", std::format("mail {} from {} ({})", m.id, m.from_name, m.from));
        return json{{"ok", true}, {"id", m.id}};
    }

    // -------------------------------------------------------------------------
    //  impl — views
    // -------------------------------------------------------------------------
    inline json impl::peers_json()
    {
        json out = json::array();
        auto metas = st.all_meta();
        std::map<std::string, peer_state> disc;
        std::map<std::string, bool> online;
        {
            std::lock_guard<std::mutex> lk(mx);
            disc = discovered;
            for (auto &[id, c] : sessions)
                online[id] = c->ready;
        }
        const long long now = (long long)std::time(nullptr);
        std::set<std::string> seen;
        for (auto &m : metas)
        {
            seen.insert(m.id);
            const bool on = online.count(m.id) && online[m.id];
            const auto it = disc.find(m.id);
            json e{{"client_id", m.id},
                   {"name", m.name.empty() ? m.id.substr(0, 8) : m.name},
                   {"paired", true},
                   {"online", on},
                   {"addr", on ? (it != disc.end() ? it->second.addr : m.addr) : m.addr},
                   {"port", on ? (it != disc.end() ? it->second.port : m.port) : m.port},
                   {"paired_at", m.paired_at},
                   {"last_seen", on ? now : m.last_seen},
                   {"via", m.via},
                   {"discovered", it != disc.end()},
                   {"pairing", it != disc.end() && it->second.pairing_open}};
            out.push_back(std::move(e));
        }
        for (auto &[id, p] : disc)
        {
            if (seen.count(id))
                continue;
            out.push_back({{"client_id", id},
                           {"name", p.name.empty() ? id.substr(0, 8) : p.name},
                           {"paired", false},
                           {"online", false},
                           {"addr", p.addr},
                           {"port", p.port},
                           {"paired_at", 0},
                           {"last_seen", p.last_seen},
                           {"via", ""},
                           {"discovered", true},
                           {"pairing", p.pairing_open}});
        }
        return out;
    }

    inline json impl::status()
    {
        json pair;
        {
            std::lock_guard<std::mutex> lk(mx);
            pair = {{"open", pairing.open},
                    {"state", pairing.state},
                    {"code", pairing.display},
                    {"expires", pairing.expires},
                    {"peer", pairing.peer},
                    {"fails", pairing.fails}};
        }
        auto [unread, total] = st.mail_counts();
        return json{{"enabled", alive.load()},
                    {"client_id", client_id},
                    {"name", name},
                    {"port", bound_port},
                    {"discovery_port", udp_sock ? opt.discovery_port : 0},
                    {"announce", opt.announce},
                    {"accept_remote", opt.accept_remote},
                    {"pairing", pair},
                    {"peers", peers_json()},
                    {"inbox", {{"unread", unread}, {"total", total}}}};
    }

    // =========================================================================
    //  service — the public face (pimpl: the header stays readable and impl
    //  internals never leak into the shell)
    // =========================================================================
    class service
    {
    public:
        using options = impl::options;
        using handler = std::function<json(const json &, std::string &)>;

        explicit service(options o) : p_(std::make_shared<impl>(std::move(o))) {}
        service(const service &) = delete;
        service &operator=(const service &) = delete;
        ~service() { p_->stop(); }

        bool start(std::string &err) { return p_->start(err); }
        void stop() { p_->stop(); }
        bool running() const { return p_->alive.load(); }
        json status() { return p_->status(); }
        std::string client_id() const { return p_->client_id; }
        std::string name() const { return p_->name; }

        void on(std::string method, handler h)
        {
            std::lock_guard<std::mutex> lk(p_->mx);
            p_->handlers[std::move(method)] = std::move(h);
        }

        // ---- pairing -------------------------------------------------------
        json pair_begin(int ttl_seconds)
        {
            if (!running())
                return json{{"ok", false}, {"error", "flowlink is not running"}};
            const int ttl = std::clamp(ttl_seconds <= 0 ? kPairTtlSeconds : ttl_seconds, 30, 3600);
            impl::pairing_state ps;
            ps.open = true;
            ps.code = pairing_normalize(pairing_code_new());
            ps.display = pairing_display(ps.code);
            ps.expires = (long long)std::time(nullptr) + ttl;
            ps.state = "waiting";
            {
                std::lock_guard<std::mutex> lk(p_->mx);
                p_->pairing = ps;
            }
            // make sure the other side can find us even if announce is off
            p_->probe_now();
            p_->announce_once(true, nullptr);
            return json{{"ok", true}, {"code", ps.display}, {"raw", ps.code},
                        {"expires", ps.expires}, {"client_id", p_->client_id}, {"name", p_->name}};
        }

        json pair_cancel()
        {
            std::lock_guard<std::mutex> lk(p_->mx);
            const bool was = p_->pairing.open;
            p_->pairing = impl::pairing_state{};
            return json{{"ok", true}, {"was_open", was}};
        }

        // `code` is what the user typed (grouping spaces are ignored); `addr`
        // is "ip" or "ip:port", empty = find the peer through discovery
        json pair_join(const std::string &code, const std::string &addr, std::string &err)
        {
            if (!running())
            {
                err = "flowlink is not running";
                return json{};
            }
            const std::string norm = pairing_normalize(code);
            std::string raw;
            if (!pairing_code_decode(norm, raw))
            {
                err = "the pairing code is malformed (512-bit, base62)";
                return json{};
            }
            std::vector<tcp::endpoint> targets;
            boost::system::error_code ec;
            std::string host = addr;
            std::uint16_t port = 0;
            const auto colon = host.rfind(':');
            if (colon != std::string::npos && host.find(':') == colon)
            {
                port = (std::uint16_t)std::atoi(host.c_str() + colon + 1);
                host = host.substr(0, colon);
            }
            if (!host.empty())
            {
                const auto a = asio::ip::make_address(host, ec);
                if (ec)
                {
                    err = "bad address: " + addr;
                    return json{};
                }
                targets.emplace_back(a, port ? port : kDefaultPort);
            }
            else
            {
                // ask the network: cells with a pairing window open answer
                p_->probe_now();
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                std::vector<impl::peer_state> cand;
                {
                    std::lock_guard<std::mutex> lk(p_->mx);
                    for (auto &[id, ps] : p_->discovered)
                        if (ps.pairing_open)
                            cand.push_back(ps);
                }
                for (auto &c : cand)
                {
                    const auto a = asio::ip::make_address(c.addr, ec);
                    if (!ec)
                        targets.emplace_back(a, c.port);
                }
                if (targets.empty())
                {
                    err = "no cell is offering a pairing window on this network — enter its address instead";
                    return json{};
                }
            }
            std::string last;
            for (auto &ep : targets)
            {
                auto c = p_->make_conn(tcp::socket(p_->io));
                c->inbound = false;
                boost::system::error_code cec;
                c->sock.connect(ep, cec);
                if (cec)
                {
                    last = std::format("{}:{} unreachable: {}", ep.address().to_string(), ep.port(), cec.message());
                    continue;
                }
                c->start_outbound("pair", norm);
                std::string why;
                if (!c->wait_ready(15000, why))
                {
                    last = why;
                    c->close("pairing failed");
                    continue;
                }
                return json{{"ok", true},
                            {"client_id", c->peer_id},
                            {"name", c->peer_name},
                            {"addr", ep.address().to_string()},
                            {"port", ep.port()}};
            }
            err = last.empty() ? "pairing failed" : last;
            return json{};
        }

        // ---- peers ---------------------------------------------------------
        json peers() { return p_->peers_json(); }
        bool forget(const std::string &id, std::string &err)
        {
            std::shared_ptr<impl::conn> c;
            {
                std::lock_guard<std::mutex> lk(p_->mx);
                auto it = p_->sessions.find(id);
                if (it != p_->sessions.end())
                    c = it->second;
            }
            if (c)
                c->close("forgotten");
            if (!p_->st.forget(id))
            {
                err = "unknown peer";
                return false;
            }
            std::lock_guard<std::mutex> lk(p_->mx);
            p_->discovered.erase(id);
            return true;
        }

        // ---- mailbox -------------------------------------------------------
        json send_mail(const std::string &peer, const json &item, std::string &err)
        {
            std::string why;
            auto c = p_->session_for(peer, 8000, why);
            if (!c)
            {
                err = why;
                return json{};
            }
            json r;
            if (!c->request("mail.deliver", item, 15000, r, why))
            {
                err = why;
                return json{};
            }
            return r;
        }
        json inbox()
        {
            json out = json::array();
            for (auto &m : p_->st.list_mail())
                out.push_back({{"id", m.id}, {"from", m.from}, {"from_name", m.from_name},
                               {"subject", m.subject}, {"note", m.note}, {"kind", m.kind},
                               {"created", m.created}, {"read", m.read},
                               {"size", m.payload.is_null() ? 0 : m.payload.dump().size()}});
            return out;
        }
        json read_mail(const std::string &id)
        {
            auto m = p_->st.read_mail(id);
            if (!m)
                return json{};
            p_->st.mark_read(id, true);
            return json{{"id", m->id}, {"from", m->from}, {"from_name", m->from_name},
                        {"subject", m->subject}, {"note", m->note}, {"kind", m->kind},
                        {"created", m->created}, {"read", true}, {"payload", m->payload}};
        }
        bool drop_mail(const std::string &id) { return p_->st.drop_mail(id); }
        json mark_mail(const std::string &id, bool read)
        {
            return json{{"ok", p_->st.mark_read(id, read)}};
        }

        // ---- generic rpc (fs.*, tw.*, …) -----------------------------------
        json call(const std::string &peer, const std::string &method, const json &params,
                  int timeout_ms, std::string &err)
        {
            std::string why;
            auto c = p_->session_for(peer, 10000, why);
            if (!c)
            {
                err = why;
                return json{};
            }
            json r;
            if (!c->request(method, params, timeout_ms, r, why))
            {
                err = why;
                return json{};
            }
            return r;
        }

        json probe()
        {
            p_->probe_now();
            return json{{"ok", true}};
        }
        json discovered()
        {
            json out = json::array();
            std::lock_guard<std::mutex> lk(p_->mx);
            for (auto &[id, ps] : p_->discovered)
                out.push_back({{"client_id", id}, {"name", ps.name}, {"addr", ps.addr},
                               {"port", ps.port}, {"last_seen", ps.last_seen},
                               {"pairing", ps.pairing_open}});
            return out;
        }
        void log(const std::string &level, const std::string &msg) { p_->log(level, msg); }
        void emit(const json &ev) { p_->emit(ev); }

    private:
        std::shared_ptr<impl> p_;
    };
} // namespace cell::flowlink
