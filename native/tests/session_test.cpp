// SPDX-License-Identifier: MIT
// Synthetic peers only. Host loopback tests do not certify physical tvOS support.
#include "companion/session.hpp"
#include "companion/codec.hpp"
#include <openssl/bn.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace companion;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F body, const char* message) {
    bool failed = false; try { body(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
struct Socket {
    int fd;
    explicit Socket(int fd) : fd(fd) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    Socket(const Socket&) = delete;
};
struct Listener : Socket {
    std::uint16_t port = 0;
    explicit Listener(bool ipv6 = false) : Socket(::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0)) {
        check(fd >= 0, "listener socket"); int one = 1; ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (ipv6) {
            sockaddr_in6 addr{}; addr.sin6_family = AF_INET6; addr.sin6_addr = in6addr_loopback;
            check(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "IPv6 bind");
            socklen_t len = sizeof(addr); check(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0, "IPv6 name"); port = ntohs(addr.sin6_port);
        } else {
            sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            check(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind");
            socklen_t len = sizeof(addr); check(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0, "name"); port = ntohs(addr.sin_port);
        }
        check(::listen(fd, 4) == 0, "listen");
    }
    int accept() {
        pollfd event{fd, POLLIN, 0}; check(::poll(&event, 1, 3000) == 1, "accept timeout");
        int result = ::accept(fd, nullptr, nullptr); check(result >= 0, "accept");
        timeval timeout{3, 0}; ::setsockopt(result, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(result, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)); return result;
    }
};
struct Peer : Socket {
    Bytes rx, tx; std::uint64_t rc = 0, tc = 0;
    explicit Peer(int fd) : Socket(fd) {}
    void write(const Bytes& value) {
        std::size_t pos = 0;
        while (pos < value.size()) {
            auto n = ::send(fd, value.data() + pos, value.size() - pos, MSG_NOSIGNAL);
            check(n > 0, "peer send"); pos += static_cast<std::size_t>(n);
        }
    }
    Bytes read_exact(std::size_t size) {
        Bytes out(size); std::size_t pos = 0;
        while (pos < size) {
            auto n = ::recv(fd, out.data() + pos, size - pos, 0);
            check(n > 0, "peer receive"); pos += static_cast<std::size_t>(n);
        }
        return out;
    }
    std::pair<int, Dict> read() {
        auto header = read_exact(4);
        std::size_t size = (std::size_t(header[1]) << 16) | (std::size_t(header[2]) << 8) | header[3];
        check(size > 0 && size <= 1048576, "peer frame bound"); auto payload = read_exact(size);
        if (!rx.empty()) payload = aead(false, rx, counter_nonce(rc++), payload, header);
        return {header[0], opack_decode(payload).dict()};
    }
    void send(int type, const Dict& message, bool tamper_tag = false, bool tamper_header = false) {
        auto payload = opack_encode(message); auto size = payload.size() + (tx.empty() ? 0 : 16);
        Bytes header{static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
        if (!tx.empty()) payload = aead(true, tx, counter_nonce(tc++), payload, header);
        if (tamper_tag) payload.back() ^= 1;
        if (tamper_header) header[0] ^= 1;
        // Deliberately fragment headers to exercise full reads.
        write(Bytes(header.begin(), header.begin() + 2));
        write(Bytes(header.begin() + 2, header.end())); write(payload);
    }
    std::pair<Dict, Tlv> fields(int type) {
        auto msg = read(); check(msg.first == type, "auth frame type");
        return {msg.second, tlv_decode(msg.second.at("_pd").blob())};
    }
    void auth(int type, const Dict& request, std::vector<std::pair<std::uint8_t, Bytes>> fields) {
        send(type, {{"_x", request.at("_x")}, {"_pd", tlv_encode(fields)}});
    }
    void response(const Dict& request, bool session = false, bool noisy = true) {
        if (noisy) {
            write({1, 0, 0, 0}); // NoOp must not consume an AEAD counter.
            send(8, {{"_t", 1}, {"_i", "_unused"}, {"_c", Dict{}}});
            send(8, {{"_t", 3}, {"_x", 999999}, {"_c", Dict{}}});
        }
        send(8, {{"_t", 3}, {"_x", request.at("_x")}, {"_c", session ? Dict{{"_sid", 44}} : Dict{}}});
    }
    void eof() { char byte; check(::recv(fd, &byte, 1, 0) == 0, "client must close peer"); }
};
const Bytes tv_seed(32, 42);
Credentials saved() { return {bytes("client-id"), Bytes(32, 11), bytes("synthetic-tv"), ed25519_public_key(tv_seed)}; }

// Independent SRP server arithmetic (not the implementation's SRP client).
using Big = std::unique_ptr<BIGNUM, decltype(&BN_clear_free)>;
Big big() { return Big(BN_new(), BN_clear_free); }
Big bn(const Bytes& data) { return Big(BN_bin2bn(data.data(), static_cast<int>(data.size()), nullptr), BN_clear_free); }
Bytes binary(const BIGNUM* value, int padded = 0) {
    Bytes out(padded ? padded : BN_num_bytes(value));
    if (padded) check(BN_bn2binpad(value, out.data(), padded) == padded, "BN pad");
    else check(BN_bn2bin(value, out.data()) == static_cast<int>(out.size()), "BN encode");
    return out;
}
struct SrpServer {
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx{BN_CTX_new(), BN_CTX_free};
    Big n{BN_get_rfc3526_prime_3072(nullptr), BN_clear_free}, g = big(), b = bn(random_bytes(32)), v = big(), B = big();
    Bytes salt = random_bytes(16), key;
    SrpServer() {
        check(BN_set_word(g.get(), 5) == 1, "SRP generator");
        auto x = bn(sha512({salt, sha512({bytes("Pair-Setup:1234")})}));
        check(BN_mod_exp(v.get(), g.get(), x.get(), n.get(), ctx.get()) == 1, "SRP verifier");
        auto k = bn(sha512({binary(n.get(), 384), binary(g.get(), 384)}));
        auto gb = big(), kv = big();
        check(BN_mod_exp(gb.get(), g.get(), b.get(), n.get(), ctx.get()) == 1, "SRP g^b");
        check(BN_mod_mul(kv.get(), k.get(), v.get(), n.get(), ctx.get()) == 1, "SRP kv");
        check(BN_mod_add(B.get(), kv.get(), gb.get(), n.get(), ctx.get()) == 1, "SRP B");
    }
    Bytes verify(const Bytes& client, const Bytes& received_proof) {
        auto a = bn(client), u = bn(sha512({binary(a.get(), 384), binary(B.get(), 384)}));
        auto vu = big(), base = big(), secret = big();
        check(BN_mod_exp(vu.get(), v.get(), u.get(), n.get(), ctx.get()) == 1, "SRP v^u");
        check(BN_mod_mul(base.get(), a.get(), vu.get(), n.get(), ctx.get()) == 1, "SRP base");
        check(BN_mod_exp(secret.get(), base.get(), b.get(), n.get(), ctx.get()) == 1, "SRP shared");
        key = sha512({binary(secret.get())});
        auto hn = sha512({binary(n.get())}), hg = sha512({binary(g.get())});
        for (std::size_t i = 0; i < hn.size(); ++i) hn[i] ^= hg[i];
        auto proof = sha512({hn, sha512({bytes("Pair-Setup")}), salt, client, binary(B.get()), key});
        check(proof == received_proof, "independent server verifies client SRP proof"); return sha512({client, proof, key});
    }
};
Credentials pair_server(Peer& p, bool bad_proof = false) {
    SrpServer srp; auto m1 = p.fields(3); check(m1.second.at(6) == Bytes{1}, "pair M1 state");
    // Wrong auth XID is ignored, even if it carries an explicit error.
    p.send(4, {{"_x", 999999}, {"_ec", 1}});
    p.auth(4, m1.first, {{6, {2}}, {2, srp.salt}, {3, binary(srp.B.get())}});
    auto m3 = p.fields(4); auto proof = srp.verify(m3.second.at(3), m3.second.at(4));
    if (bad_proof) { proof[0] ^= 1; p.auth(4, m3.first, {{6, {4}}, {4, proof}}); p.eof(); return {}; }
    p.auth(4, m3.first, {{6, {4}}, {4, proof}});
    auto key = hkdf_sha512(srp.key, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info");
    auto m5 = p.fields(4); auto identity = tlv_decode(aead(false, key, named_nonce("PS-Msg05"), m5.second.at(5)));
    auto client_id = identity.at(1), client_key = identity.at(3);
    ed25519_verify(client_key, concat({hkdf_sha512(srp.key, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info"), client_id, client_key}), identity.at(10));
    auto peer = saved();
    auto signature = ed25519_sign(tv_seed, concat({hkdf_sha512(srp.key, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info"), peer.device_id, peer.device_public_key}));
    p.auth(4, m5.first, {{6, {6}}, {5, aead(true, key, named_nonce("PS-Msg06"), tlv_encode({{1, peer.device_id}, {3, peer.device_public_key}, {10, signature}}))}});
    peer.client_id = client_id;
    // This test-only field carries the public key for verifying the next connection.
    peer.client_secret = client_key; return peer;
}
void verify_server(Peer& p, const Credentials& credentials, const Bytes& client_key, int defect = 0) {
    auto m1 = p.fields(5); auto client_pub = m1.second.at(3);
    auto private_key = random_bytes(32), pub = x25519_public_key(private_key);
    auto shared = x25519_shared_secret(private_key, client_pub);
    auto key = hkdf_sha512(shared, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");
    auto id = defect == 1 ? bytes("different-tv") : credentials.device_id;
    auto signature = ed25519_sign(defect == 2 ? Bytes(32, 7) : tv_seed, concat({pub, id, client_pub}));
    auto encrypted = aead(true, key, named_nonce("PV-Msg02"), tlv_encode({{1, id}, {10, signature}}));
    if (defect == 3) encrypted.back() ^= 1;
    p.auth(6, m1.first, {{6, {2}}, {3, pub}, {5, encrypted}});
    if (defect) { p.eof(); return; }
    auto m3 = p.fields(6); auto identity = tlv_decode(aead(false, key, named_nonce("PV-Msg03"), m3.second.at(5)));
    check(identity.at(1) == credentials.client_id, "saved client identity");
    ed25519_verify(client_key, concat({client_pub, credentials.client_id, pub}), identity.at(10));
    p.auth(6, m3.first, {{6, {4}}});
    p.rx = hkdf_sha512(shared, "", "ClientEncrypt-main"); p.tx = hkdf_sha512(shared, "", "ServerEncrypt-main");
}
void handshake(Peer& p) {
    auto system = p.read(); check(system.first == 8 && system.second.at("_i").string() == "_systemInfo", "systemInfo handshake");
    check(system.second.at("_c").dict().at("model").string() == "HarmonyOS", "Harmony model"); p.response(system.second);
    auto session = p.read(); check(session.second.at("_i").string() == "_sessionStart", "button-only session handshake"); p.response(session.second, true);
}

void full_lifecycle() {
    Listener listener;
    auto server = std::async(std::launch::async, [&] {
        Credentials paired;
        { Peer p(listener.accept()); paired = pair_server(p); p.eof(); }
        Peer p(listener.accept()); verify_server(p, paired, paired.client_secret); handshake(p);
        for (auto button : {1, 2, 3, 4, 5, 6, 7, 14}) for (auto state : {1, 2}) {
            auto message = p.read(); check(message.first == 8, "HID frame");
            check(message.second.at("_i").string() == "_hidC", "HID command");
            auto content = message.second.at("_c").dict();
            check(content.at("_hidC").uint() == static_cast<unsigned>(button), "button code");
            check(content.at("_hBtS").uint() == static_cast<unsigned>(state), "down then up"); p.response(message.second);
        }
        p.eof();
    });
    Session session(1500ms); auto epoch = session.epoch(); session.begin_pairing("127.0.0.1", listener.port, epoch);
    auto credentials = session.finish_pairing("1234", epoch);
    auto encoded = credentials.encode(); auto decoded = Credentials::decode(encoded);
    check(decoded.client_id == credentials.client_id && decoded.client_secret == credentials.client_secret && decoded.device_id == credentials.device_id && decoded.device_public_key == credentials.device_public_key, "credential round trip");
    session.connect("127.0.0.1", listener.port, decoded, epoch);
    for (auto button : {1, 2, 3, 4, 5, 6, 7, 14}) session.press(button, epoch);
    session.disconnect(epoch); server.get();
}
void credential_validation() {
    auto value = saved().encode(); auto trailing = value; trailing.push_back(0);
    rejects([&] { Credentials::decode(trailing); }, "trailing credentials rejected");
    auto wrong = value; wrong[0] = 0; rejects([&] { Credentials::decode(wrong); }, "magic rejected");
    for (std::size_t n = 0; n < value.size(); ++n) rejects([&] { Credentials::decode(Bytes(value.begin(), value.begin() + n)); }, "truncated credentials rejected");
    wrong = value; wrong[4] = 255; rejects([&] { Credentials::decode(wrong); }, "oversized identity rejected");
    auto invalid = saved(); invalid.client_secret.pop_back(); rejects([&] { invalid.encode(); }, "bad secret rejected");
}
void bad_pairing_proof() {
    Listener listener; auto server = std::async(std::launch::async, [&] { Peer p(listener.accept()); pair_server(p, true); });
    Session session(1s); auto epoch = session.epoch(); session.begin_pairing("127.0.0.1", listener.port, epoch);
    rejects([&] { session.finish_pairing("1234", epoch); }, "bad SRP server proof rejected"); server.get();
}
void identity_rejection() {
    for (int defect : {1, 2, 3}) {
        Listener listener; auto credentials = saved();
        auto server = std::async(std::launch::async, [&] { Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret), defect); });
        Session session(1s); rejects([&] { session.connect("127.0.0.1", listener.port, credentials, session.epoch()); }, "untrusted TV rejected"); server.get();
    }
}
void frame_rejection() {
    for (int defect : {1, 2}) {
        Listener listener; auto credentials = saved();
        auto server = std::async(std::launch::async, [&] {
            Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret));
            auto request = p.read();
            p.send(8, {{"_t", 3}, {"_x", request.second.at("_x")}, {"_c", Dict{}}}, defect == 1, defect == 2); p.eof();
        });
        Session session(1s); rejects([&] { session.connect("127.0.0.1", listener.port, credentials, session.epoch()); }, "tampered encrypted frame rejected"); server.get();
    }
    for (auto frame : {Bytes{4, 0x10, 0, 1}, Bytes{4, 0, 0, 0}, Bytes{4, 0, 0, 1, 0xff}}) {
        Listener listener; auto server = std::async(std::launch::async, [&] { Peer p(listener.accept()); p.fields(3); p.write(frame); p.eof(); });
        Session session(1s); rejects([&] { session.begin_pairing("127.0.0.1", listener.port, session.epoch()); }, "malformed frame rejected"); server.get();
    }
}
void total_deadline() {
    for (bool partial : {false, true}) {
        Listener listener;
        auto server = std::async(std::launch::async, [&] {
            Peer p(listener.accept()); p.fields(3);
            if (partial) { p.write({4, 0}); p.eof(); return; }
            for (int i = 0; i < 100; ++i) {
                try { p.send(7, {{"unrelated", true}}); std::this_thread::sleep_for(5ms); }
                catch (const std::exception&) { return; }
            }
            throw std::runtime_error("unrelated flood never timed out");
        });
        Session session(120ms); auto start = std::chrono::steady_clock::now();
        rejects([&] { session.begin_pairing("127.0.0.1", listener.port, session.epoch()); }, "deadline rejects");
        check(std::chrono::steady_clock::now() - start < 1s, "total response deadline bounded"); server.get();
    }
}
void cancellation() {
    Listener listener; std::promise<void> received; auto ready = received.get_future();
    auto server = std::async(std::launch::async, [&] { Peer p(listener.accept()); p.fields(3); received.set_value(); p.eof(); });
    Session session(2s); auto old = session.epoch();
    auto call = std::async(std::launch::async, [&] { rejects([&] { session.begin_pairing("127.0.0.1", listener.port, old); }, "cancelled operation rejects"); });
    check(ready.wait_for(1s) == std::future_status::ready, "request reached peer"); auto start = std::chrono::steady_clock::now();
    session.cancel(); call.get(); check(std::chrono::steady_clock::now() - start < 300ms, "cancel wakes blocked socket"); server.get();
    rejects([&] { session.begin_pairing("127.0.0.1", listener.port, old); }, "stale epoch cannot open socket");
    pollfd pending{listener.fd, POLLIN, 0}; check(::poll(&pending, 1, 30) == 0, "cancelled queued operation did not connect");
    session.disconnect(session.epoch());
}
void release_on_failure() {
    Listener listener; auto credentials = saved();
    auto server = std::async(std::launch::async, [&] {
        Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret)); handshake(p);
        auto down = p.read(); check(down.second.at("_c").dict().at("_hBtS").uint() == 1, "down before failure");
        p.send(8, {{"_t", 3}, {"_x", down.second.at("_x")}, {"_ec", 1}});
        auto release = p.read(); check(release.second.at("_c").dict().at("_hBtS").uint() == 2, "best-effort release after rejected down");
        p.eof();
    });
    Session session(1s); auto epoch = session.epoch(); session.connect("127.0.0.1", listener.port, credentials, epoch);
    rejects([&] { session.press(6, epoch); }, "rejected down is not a successful tap"); server.get();
    rejects([&] { session.press(6, epoch); }, "failed session closed");
}
void pipelined_release_and_cancel() {
    Listener listener; auto credentials = saved(); std::promise<void> released;
    auto ready = released.get_future();
    auto server = std::async(std::launch::async, [&] {
        Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret)); handshake(p);
        auto down = p.read(); auto up = p.read();
        check(down.second.at("_c").dict().at("_hBtS").uint() == 1, "pipelined down");
        check(up.second.at("_c").dict().at("_hBtS").uint() == 2, "up sent before any down acknowledgement");
        // First tap replies arrive in reverse XID order.
        p.response(up.second); p.response(down.second);
        down = p.read(); up = p.read();
        check(up.second.at("_c").dict().at("_hBtS").uint() == 2, "cancelled tap release already sent");
        released.set_value(); p.eof();
    });
    Session session(2s); auto epoch = session.epoch(); session.connect("127.0.0.1", listener.port, credentials, epoch);
    session.press(6, epoch);
    auto call = std::async(std::launch::async, [&] { rejects([&] { session.press(6, epoch); }, "tap cancel rejects pending acknowledgement"); });
    check(ready.wait_for(1s) == std::future_status::ready, "both button edges reached peer"); session.cancel(); call.get(); server.get();
}
void stale_epoch_preserves_new_session() {
    Listener listener; auto credentials = saved();
    auto server = std::async(std::launch::async, [&] {
        Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret)); handshake(p);
        auto down = p.read(); auto up = p.read(); p.response(down.second); p.response(up.second); p.eof();
    });
    Session session(1s); auto stale = session.epoch(); session.cancel(); auto epoch = session.epoch();
    session.connect("127.0.0.1", listener.port, credentials, epoch);
    rejects([&] { session.begin_pairing("127.0.0.1", listener.port, stale); }, "stale operation rejects before changing new state");
    rejects([&] { session.disconnect(stale); }, "stale disconnect does not close new state");
    session.press(6, epoch); session.disconnect(epoch); server.get();
}
void concurrent_cancel_and_new_epoch() {
    constexpr int rounds = 32;
    Listener listener; auto credentials = saved();
    auto server = std::async(std::launch::async, [&] {
        for (int round = 0; round < rounds; ++round) {
            Peer p(listener.accept()); verify_server(p, credentials, ed25519_public_key(credentials.client_secret)); handshake(p);
            auto down = p.read(); auto up = p.read(); p.response(down.second, false, false); p.response(up.second, false, false); p.eof();
        }
    });
    Session session(1s);
    for (int round = 0; round < rounds; ++round) {
        auto before = session.epoch();
        auto cancel = std::async(std::launch::async, [&] { session.cancel(); });
        // Deliberately start new work as soon as the new epoch is visible, before
        // joining cancel. Delayed cleanup must not close this newer connection.
        while (session.epoch() == before) std::this_thread::yield();
        auto epoch = session.epoch();
        session.connect("127.0.0.1", listener.port, credentials, epoch);
        cancel.get();
        session.press(6, epoch); session.disconnect(epoch);
    }
    server.get();
}
void ipv6_and_numeric_addresses() {
    Listener listener(true);
    auto server = std::async(std::launch::async, [&] { Peer p(listener.accept()); auto request = p.fields(3); p.auth(4, request.first, {{6, {2}}, {2, Bytes(16, 1)}, {3, Bytes{1}}}); p.eof(); });
    Session session(1s); auto epoch = session.epoch(); session.begin_pairing("::1", listener.port, epoch); session.disconnect(epoch); server.get();
    rejects([&] { session.begin_pairing("localhost", listener.port, epoch); }, "hostnames are not accepted");
    rejects([&] { session.begin_pairing("127.0.0.1", 0, epoch); }, "zero port rejected");
}
} // namespace
int main() {
    int passed = 0;
    try {
        for (auto test : std::vector<std::pair<const char*, std::function<void()>>>{
            {"pair, verify, handshake, all buttons, NoOp and XID interleaving", full_lifecycle},
            {"strict credential serialization", credential_validation},
            {"reject forged SRP proof", bad_pairing_proof},
            {"reject wrong identity, forged signature and verify AEAD tag", identity_rejection},
            {"reject frame tag/header tampering and malformed frames", frame_rejection},
            {"total deadline during frame flood and partial header", total_deadline},
            {"shutdown cancellation and stale epoch", cancellation},
            {"best-effort button release after failure", release_on_failure},
            {"pipelined release, reordered acknowledgements and cancel", pipelined_release_and_cancel},
            {"stale epochs preserve a newly connected session", stale_epoch_preserves_new_session},
            {"concurrent cancel preserves immediately started fresh-epoch work", concurrent_cancel_and_new_epoch},
            {"numeric IPv4/IPv6 and dynamic ports", ipv6_and_numeric_addresses}
        }) { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
    } catch (const std::exception& error) { std::cerr << "FAIL after " << passed << " tests: " << error.what() << '\n'; return 1; }
    std::cout << passed << " native session tests passed\n"; return 0;
}
