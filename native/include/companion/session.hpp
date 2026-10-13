// SPDX-License-Identifier: MIT
#pragma once

#include "companion/crypto.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace companion {

// Secret material. Persist only inside an authenticated, encrypted platform store.
struct Credentials {
    Bytes client_id;
    Bytes client_secret;
    Bytes device_id;
    Bytes device_public_key;
    Credentials() = default;
    Credentials(Bytes client_id, Bytes client_secret, Bytes device_id, Bytes device_public_key);
    Credentials(const Credentials&) = default;
    Credentials& operator=(const Credentials&);
    Credentials(Credentials&&) noexcept = default;
    Credentials& operator=(Credentials&&) noexcept;
    ~Credentials();
    void validate() const;
    Bytes encode() const;
    static Credentials decode(const Bytes&);
};

// Blocking protocol engine. Operations must be serialized on a background worker.
// cancel() and epoch() alone are thread-safe alongside a running operation.
// Capture an epoch when enqueueing: cancelled/queued work must not reopen a socket.
class Session {
public:
    using Epoch = std::uint64_t;
    explicit Session(std::chrono::milliseconds timeout = std::chrono::seconds(10));
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Epoch epoch() const noexcept;
    void cancel() noexcept;
    void begin_pairing(const std::string& numeric_host, std::uint16_t port, Epoch);
    Credentials finish_pairing(const std::string& pin, Epoch);
    void connect(const std::string& numeric_host, std::uint16_t port, const Credentials&, Epoch);
    void press(std::uint32_t button, Epoch);
    void disconnect(Epoch);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace companion
