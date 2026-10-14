// SPDX-License-Identifier: MIT
#include "companion/session.hpp"
#include "companion/codec.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace companion {
namespace {
constexpr std::size_t kFrameLimit = 1024 * 1024;
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
using Dict = Value::Dict;

[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
void require(bool condition, const char* message) { if (!condition) fail(message); }
const Bytes& field(const Tlv& tlv, std::uint8_t key) {
    auto it = tlv.find(key);
    if (it == tlv.end()) fail("Missing authentication field");
    return it->second;
}
bool number(const Value* value, std::uint64_t expected) {
    return value && std::holds_alternative<std::uint64_t>(value->data) && value->uint() == expected;
}
const Value* lookup(const Dict& value, const std::string& key) {
    auto it = value.find(key); return it == value.end() ? nullptr : &it->second;
}
void reject_error(const Dict& value, const char* message) {
    require(!lookup(value, "_em") && !lookup(value, "_ec"), message);
}
struct Wipe {
    Bytes& value;
    ~Wipe() { wipe(value); }
};
struct Socket {
    explicit Socket(int value) : fd(value) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    int fd;
};
std::string client_uuid() {
    Bytes data = random_bytes(16);
    data[6] = (data[6] & 15) | 64;
    data[8] = (data[8] & 63) | 128;
    std::ostringstream text; text << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text << '-';
        text << std::setw(2) << unsigned(data[i]);
    }
    return text.str();
}
std::string public_id(const Bytes& id) {
    auto hash = sha512({id});
    hash[0] = (hash[0] & 0xfc) | 2;
    std::ostringstream text; text << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t i = 0; i < 6; ++i) {
        if (i) text << ':';
        text << std::setw(2) << unsigned(hash[i]);
    }
    return text.str();
}
} // namespace

Credentials::Credentials(Bytes id, Bytes seed, Bytes peer, Bytes key)
    : client_id(std::move(id)), client_secret(std::move(seed)), device_id(std::move(peer)), device_public_key(std::move(key)) {
    try { validate(); } catch (...) { wipe(client_secret); throw; }
}
Credentials::~Credentials() { wipe(client_secret); }
Credentials& Credentials::operator=(const Credentials& value) {
    if (this != &value) {
        wipe(client_secret); client_id = value.client_id; client_secret = value.client_secret;
        device_id = value.device_id; device_public_key = value.device_public_key;
    }
    return *this;
}
Credentials& Credentials::operator=(Credentials&& value) noexcept {
    if (this != &value) {
        wipe(client_secret); client_id = std::move(value.client_id); client_secret = std::move(value.client_secret);
        device_id = std::move(value.device_id); device_public_key = std::move(value.device_public_key);
    }
    return *this;
}
void Credentials::validate() const {
    require(!client_id.empty() && client_id.size() <= 128 && client_secret.size() == 32 &&
            !device_id.empty() && device_id.size() <= 128 && device_public_key.size() == 32,
            "Invalid saved credentials");
}
Bytes Credentials::encode() const {
    validate(); Bytes out{'H', 'C', 'P', '1'};
    for (const Bytes* value : {&client_id, &client_secret, &device_id, &device_public_key}) {
        out.push_back(static_cast<std::uint8_t>(value->size() >> 8));
        out.push_back(static_cast<std::uint8_t>(value->size()));
        out.insert(out.end(), value->begin(), value->end());
    }
    return out;
}
Credentials Credentials::decode(const Bytes& in) {
    require(in.size() >= 4 && in.size() <= 332 && std::equal(in.begin(), in.begin() + 4, "HCP1"), "Invalid saved credentials");
    Credentials result; std::size_t pos = 4;
    for (Bytes* value : {&result.client_id, &result.client_secret, &result.device_id, &result.device_public_key}) {
        require(pos + 2 <= in.size(), "Truncated saved credentials");
        std::size_t size = (std::size_t(in[pos]) << 8) | in[pos + 1]; pos += 2;
        require(size <= 128 && size <= in.size() - pos, "Invalid saved credentials length");
        value->assign(in.begin() + pos, in.begin() + pos + size); pos += size;
    }
    require(pos == in.size(), "Trailing saved credentials data"); result.validate(); return result;
}

class Session::Impl {
public:
    explicit Impl(std::chrono::milliseconds value) : timeout(value) {
        require(value.count() > 0 && value <= std::chrono::minutes(1), "Invalid operation timeout");
    }
    ~Impl() { cancel(); reset(); }
    std::atomic<Epoch> generation{1};
    std::mutex socket_mutex;
    std::mutex operation_mutex;
    std::shared_ptr<Socket> socket;
    std::chrono::milliseconds timeout;
    Bytes write_key, read_key, salt, server_public;
    std::uint64_t write_counter = 0, read_counter = 0, xid = 1;
    bool connected = false, usable = false;
    std::uint32_t held_button = 0;
    bool select_held = false, touch_active = false;
    std::uint32_t touch_x = 0, touch_y = 0;
    Deadline touch_epoch{};
    std::uint64_t touch_ns = 0;

    void check(Epoch epoch) const { require(generation.load(std::memory_order_acquire) == epoch, "Operation cancelled"); }
    void cancel() noexcept {
        std::lock_guard<std::mutex> guard(socket_mutex);
        generation.fetch_add(1, std::memory_order_acq_rel);
        // shutdown wakes poll/recv immediately. A running syscall holds the
        // socket lifetime until it unwinds, avoiding descriptor-reuse races.
        if (socket) ::shutdown(socket->fd, SHUT_RDWR);
        socket.reset();
        // Keep epoch publication, socket removal and idle cleanup in one critical
        // section. Otherwise newly enqueued work could finish before this cancel
        // resumes and its delayed reset could erase that newer session.
        // Never wait for operation_mutex while holding socket_mutex: the active
        // operation takes these locks in the other order and wipes on unwind.
        std::unique_lock<std::mutex> idle(operation_mutex, std::try_to_lock);
        if (idle.owns_lock()) reset_fields();
    }
    void reset() noexcept {
        {
            std::lock_guard<std::mutex> guard(socket_mutex);
            if (socket) ::shutdown(socket->fd, SHUT_RDWR);
            socket.reset();
        }
        reset_fields();
    }
    void reset_fields() noexcept {
        connected = false; usable = false; held_button = 0;
        select_held = false; touch_active = false; touch_x = 0; touch_y = 0;
        touch_epoch = {}; touch_ns = 0;
        wipe(write_key); wipe(read_key); wipe(salt); wipe(server_public);
        write_counter = 0; read_counter = 0; xid = 1;
    }
    struct OperationGuard {
        Impl& owner;
        Epoch expected;
        std::unique_lock<std::mutex> lock;
        OperationGuard(Impl& value, Epoch epoch)
            : owner(value), expected(epoch), lock(value.operation_mutex) { owner.check(epoch); }
        ~OperationGuard() {
            // Synchronize the final epoch check and operation unlock with cancel.
            // Either this worker wipes cancelled state or cancel observes idle.
            std::lock_guard<std::mutex> socket_guard(owner.socket_mutex);
            if (owner.generation.load(std::memory_order_acquire) != expected) owner.reset_fields();
            lock.unlock();
        }
    };
    std::shared_ptr<Socket> current(Epoch epoch) {
        std::lock_guard<std::mutex> guard(socket_mutex); check(epoch);
        require(bool(socket), "Apple TV is disconnected"); return socket;
    }
    void ready(const std::shared_ptr<Socket>& sock, short event, Deadline end, Epoch epoch) {
        for (;;) {
            check(epoch);
            auto now = Clock::now(); require(now < end, "Apple TV response timed out");
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
            pollfd fd{sock->fd, event, 0};
            int rc = ::poll(&fd, 1, static_cast<int>(std::max<std::int64_t>(1, remaining)));
            check(epoch);
            if (rc < 0 && errno == EINTR) continue;
            require(rc >= 0, "Apple TV socket poll failed");
            require(rc != 0, "Apple TV response timed out");
            require(!(fd.revents & POLLNVAL), "Apple TV socket is invalid");
            if (fd.revents & (event | POLLERR | POLLHUP)) return;
        }
    }
    void transfer(bool writing, Bytes& value, Deadline end, Epoch epoch) {
        auto sock = current(epoch); std::size_t offset = 0;
        while (offset < value.size()) {
            ready(sock, writing ? POLLOUT : POLLIN, end, epoch);
            auto size = value.size() - offset;
            ssize_t count;
            if (writing) {
#ifdef MSG_NOSIGNAL
                count = ::send(sock->fd, value.data() + offset, size, MSG_NOSIGNAL);
#else
                count = ::send(sock->fd, value.data() + offset, size, 0);
#endif
            } else count = ::recv(sock->fd, value.data() + offset, size, 0);
            check(epoch);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            require(count > 0, writing ? "Apple TV send failed" : "Apple TV disconnected");
            offset += static_cast<std::size_t>(count);
        }
    }
    void open(const std::string& host, std::uint16_t port, Epoch epoch) {
        check(epoch); reset(); check(epoch);
        require(port != 0 && !host.empty() && host.size() <= 128, "Invalid Apple TV address or port");
        sockaddr_storage address{}; socklen_t length; int family;
        auto* v4 = reinterpret_cast<sockaddr_in*>(&address);
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&address);
        // Numeric addresses only: discovery supplies a freshly resolved address/port.
        // A numeric IPv6 interface scope is supported for link-local mDNS results.
        std::string literal = host; std::uint32_t scope = 0;
        auto percent = literal.find('%');
        if (percent != std::string::npos) {
            auto suffix = literal.substr(percent + 1); literal.resize(percent);
            require(!suffix.empty() && suffix.size() <= 10, "Invalid IPv6 scope");
            std::uint64_t parsed = 0;
            for (char ch : suffix) { require(ch >= '0' && ch <= '9', "IPv6 scope must be numeric"); parsed = parsed * 10 + unsigned(ch - '0'); }
            require(parsed > 0 && parsed <= UINT32_MAX, "Invalid IPv6 scope"); scope = static_cast<std::uint32_t>(parsed);
        }
        if (!scope && ::inet_pton(AF_INET, literal.c_str(), &v4->sin_addr) == 1) {
            family = AF_INET; v4->sin_family = AF_INET; v4->sin_port = htons(port); length = sizeof(sockaddr_in);
        } else {
            require(::inet_pton(AF_INET6, literal.c_str(), &v6->sin6_addr) == 1, "Use a numeric IPv4 or IPv6 address");
            family = AF_INET6; v6->sin6_family = AF_INET6; v6->sin6_port = htons(port); v6->sin6_scope_id = scope; length = sizeof(sockaddr_in6);
        }
        std::shared_ptr<Socket> sock;
        {
            std::lock_guard<std::mutex> guard(socket_mutex); check(epoch);
            int fd = ::socket(family, SOCK_STREAM, 0); require(fd >= 0, "Could not create Apple TV socket");
            sock = std::make_shared<Socket>(fd); socket = sock;
        }
        int flags = ::fcntl(sock->fd, F_GETFL, 0);
        require(flags >= 0 && ::fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK) == 0, "Could not configure Apple TV socket");
        int one = 1; require(::setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0, "Could not configure TCP socket");
#ifdef SO_NOSIGPIPE
        ::setsockopt(sock->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        int rc;
        {
            // A cancellation must not shutdown an unconnected descriptor and
            // then allow this thread to initiate a new connection afterward.
            std::lock_guard<std::mutex> guard(socket_mutex); check(epoch);
            rc = ::connect(sock->fd, reinterpret_cast<sockaddr*>(&address), length);
        }
        if (rc != 0) {
            require(errno == EINPROGRESS, "Could not connect to Apple TV");
            ready(sock, POLLOUT, Clock::now() + timeout, epoch);
            int error = 0; socklen_t size = sizeof(error);
            require(::getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0, "Could not connect to Apple TV");
        }
        check(epoch); usable = true;
    }
    std::uint64_t next_xid() {
        require(xid < std::numeric_limits<std::uint64_t>::max(), "Companion transaction counter exhausted"); return xid++;
    }
    Bytes frame(std::uint8_t type, const Dict& message) {
        Bytes payload = opack_encode(Value(message)); Wipe cleanup{payload};
        std::size_t size = payload.size() + (write_key.empty() ? 0 : 16);
        require(size > 0 && size <= kFrameLimit, "Invalid outgoing Companion frame length");
        Bytes header{type, static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
        if (!write_key.empty()) {
            require(write_counter < std::numeric_limits<std::uint64_t>::max(), "Companion send nonce exhausted");
            Bytes encrypted = aead(true, write_key, counter_nonce(write_counter++), payload, header);
            wipe(payload); payload = std::move(encrypted);
        }
        header.insert(header.end(), payload.begin(), payload.end());
        return header;
    }
    void send(std::uint8_t type, const Dict& message, Deadline end, Epoch epoch) {
        auto packet = frame(type, message); Wipe cleanup{packet};
        try { transfer(true, packet, end, epoch); }
        catch (...) { usable = false; throw; }
    }
    std::pair<std::uint8_t, Dict> receive(Deadline end, Epoch epoch) {
        Bytes header(4); transfer(false, header, end, epoch);
        std::size_t size = (std::size_t(header[1]) << 16) | (std::size_t(header[2]) << 8) | header[3];
        if (header[0] == 1 && size == 0) return {1, {}};
        require(size > 0 && size <= kFrameLimit, "Invalid Companion frame length");
        Bytes payload(size); Wipe cleanup{payload}; transfer(false, payload, end, epoch);
        if (!read_key.empty()) {
            require(size >= 16 && read_counter < std::numeric_limits<std::uint64_t>::max(), "Invalid encrypted Companion frame");
            Bytes plain = aead(false, read_key, counter_nonce(read_counter++), payload, header);
            wipe(payload); payload = std::move(plain);
        }
        Value message = opack_decode(payload);
        return {header[0], message.dict()};
    }
    Tlv auth(std::uint8_t type, std::uint8_t sequence, const std::vector<std::pair<std::uint8_t, Bytes>>& fields, Epoch epoch) {
        auto id = next_xid(); Dict request{{"_pd", tlv_encode(fields)}, {"_x", id}};
        if (type == 3 || type == 4) request.emplace("_pwTy", std::uint64_t(1));
        if (type == 5) request.emplace("_auTy", std::uint64_t(4));
        auto end = Clock::now() + timeout; send(type, request, end, epoch);
        auto expected = type == 3 ? 4 : type == 5 ? 6 : type;
        for (;;) {
            auto response = receive(end, epoch);
            if (response.first != expected) continue;
            // Some Companion auth replies omit _x. If present, it must match.
            auto response_xid = lookup(response.second, "_x");
            if (response_xid && !number(response_xid, id)) continue;
            reject_error(response.second, "Apple TV rejected authentication");
            auto data = lookup(response.second, "_pd"); require(data, "Missing pairing response");
            auto tlv = tlv_decode(data->blob());
            require(!tlv.count(7), "Apple TV rejected pairing; verify PIN and retry");
            require(field(tlv, 6) == Bytes{sequence}, "Unexpected pairing step"); return tlv;
        }
    }
    Dict command(const std::string& name, const Dict& content, Epoch epoch, std::chrono::milliseconds budget = std::chrono::milliseconds(0)) {
        auto id = next_xid(); auto end = Clock::now() + (budget.count() ? budget : timeout);
        send(8, {{"_i", name}, {"_t", std::uint64_t(2)}, {"_x", id}, {"_c", content}}, end, epoch);
        for (;;) {
            auto response = receive(end, epoch);
            if (response.first != 8 || !number(lookup(response.second, "_t"), 3) || !number(lookup(response.second, "_x"), id)) continue;
            reject_error(response.second, "Apple TV rejected command"); return response.second;
        }
    }
    void touch_event(std::uint32_t x, std::uint32_t y, std::uint32_t phase, Deadline end, Epoch epoch) {
        // steady_clock keeps _ns monotonic even if the wall clock changes. Match
        // Companion's nanoseconds-since-touch-negotiation epoch, not Unix time.
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - touch_epoch).count();
        touch_ns = std::max(touch_ns, static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
        send(8, {{"_i", "_hidT"}, {"_t", std::uint64_t(1)}, {"_x", next_xid()},
            {"_c", Dict{{"_ns", touch_ns}, {"_tFg", std::uint64_t(1)}, {"_cx", std::uint64_t(x)},
                {"_cy", std::uint64_t(y)}, {"_tPh", std::uint64_t(phase)}}}}, end, epoch);
    }
    void tap(std::uint32_t button, Epoch epoch) {
        auto down_id = next_xid(), up_id = next_xid();
        auto end = Clock::now() + timeout;
        auto down = frame(8, {{"_i", "_hidC"}, {"_t", std::uint64_t(2)}, {"_x", down_id},
            {"_c", Dict{{"_hBtS", std::uint64_t(1)}, {"_hidC", std::uint64_t(button)}}}});
        Wipe wipe_down{down};
        auto up = frame(8, {{"_i", "_hidC"}, {"_t", std::uint64_t(2)}, {"_x", up_id},
            {"_c", Dict{{"_hBtS", std::uint64_t(2)}, {"_hidC", std::uint64_t(button)}}}});
        Wipe wipe_up{up}; down.insert(down.end(), up.begin(), up.end());
        held_button = button;
        // Queue both edges together before waiting. Cancellation while awaiting an
        // acknowledgement cannot strand a button merely because the TV is slow.
        // A transport failure during a partial write cannot guarantee delivery.
        try { transfer(true, down, end, epoch); }
        catch (...) { usable = false; throw; }
        held_button = 0;
        bool down_ack = false, up_ack = false;
        while (!down_ack || !up_ack) {
            auto response = receive(end, epoch);
            if (response.first != 8 || !number(lookup(response.second, "_t"), 3)) continue;
            auto id = lookup(response.second, "_x");
            if (!number(id, down_id) && !number(id, up_id)) continue;
            reject_error(response.second, "Apple TV rejected command");
            if (number(id, down_id)) down_ack = true;
            if (number(id, up_id)) up_ack = true;
        }
    }
    void release(Epoch epoch) noexcept {
        auto button = held_button;
        bool select = select_held, touch = touch_active;
        held_button = 0; select_held = false; touch_active = false;
        if (!usable || write_key.empty()) return;
        // Only send: a failed earlier response may leave inbound framing incomplete.
        // Bound all cleanup to one 200 ms budget. A dead/cancelled transport cannot
        // guarantee release; always close it rather than replay on a new session.
        auto end = Clock::now() + std::chrono::milliseconds(200);
        try {
            for (auto code : {button, select ? std::uint32_t(6) : std::uint32_t(0)}) {
                if (code) send(8, {{"_i", "_hidC"}, {"_t", std::uint64_t(2)}, {"_x", next_xid()},
                    {"_c", Dict{{"_hBtS", std::uint64_t(2)}, {"_hidC", std::uint64_t(code)}}}}, end, epoch);
            }
            if (touch) touch_event(touch_x, touch_y, 4, end, epoch);
        } catch (...) { }
    }
};
Session::Session(std::chrono::milliseconds timeout) : impl_(std::make_unique<Impl>(timeout)) {}
Session::~Session() = default;
Session::Epoch Session::epoch() const noexcept { return impl_->generation.load(std::memory_order_acquire); }
void Session::cancel() noexcept { impl_->cancel(); }
void Session::begin_pairing(const std::string& host, std::uint16_t port, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    try {
        p.open(host, port, epoch);
        auto reply = p.auth(3, 2, {{0, {0}}, {6, {1}}}, epoch);
        p.salt = field(reply, 2); p.server_public = field(reply, 3);
        require(p.salt.size() >= 16 && p.salt.size() <= 64 && !p.server_public.empty() && p.server_public.size() <= 384, "Invalid pairing challenge");
        p.check(epoch);
    } catch (...) { p.reset(); throw; }
}
Credentials Session::finish_pairing(const std::string& pin, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    try {
        p.check(epoch);
        require(pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; }), "Enter the four-digit PIN shown on Apple TV");
        require(!p.salt.empty() && !p.server_public.empty(), "Start pairing first");
        Srp3072 srp(pin, p.salt, p.server_public);
        auto m4 = p.auth(4, 4, {{6, {3}}, {3, srp.public_key}, {4, srp.proof}}, epoch); srp.verify(field(m4, 4));
        Credentials result; result.client_secret = random_bytes(32); result.client_id = bytes(client_uuid());
        auto public_key = ed25519_public_key(result.client_secret);
        auto controller = hkdf_sha512(srp.session_key, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info"); Wipe wipe_controller{controller};
        auto key = hkdf_sha512(srp.session_key, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info"); Wipe wipe_key{key};
        auto controller_message = concat({controller, result.client_id, public_key}); Wipe wipe_controller_message{controller_message};
        auto signature = ed25519_sign(result.client_secret, controller_message);
        auto payload = tlv_encode({{1, result.client_id}, {3, public_key}, {10, signature}}); Wipe wipe_payload{payload};
        auto m6 = p.auth(4, 6, {{6, {5}}, {5, aead(true, key, named_nonce("PS-Msg05"), payload)}}, epoch);
        auto plain = aead(false, key, named_nonce("PS-Msg06"), field(m6, 5)); Wipe wipe_plain{plain};
        auto peer = tlv_decode(plain); result.device_id = field(peer, 1); result.device_public_key = field(peer, 3); result.validate();
        auto accessory = hkdf_sha512(srp.session_key, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info"); Wipe wipe_accessory{accessory};
        auto accessory_message = concat({accessory, result.device_id, result.device_public_key}); Wipe wipe_accessory_message{accessory_message};
        ed25519_verify(result.device_public_key, accessory_message, field(peer, 10));
        p.check(epoch); p.reset(); return result;
    } catch (...) { p.reset(); throw; }
}
void Session::connect(const std::string& host, std::uint16_t port, const Credentials& credentials, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    try {
        credentials.validate(); p.open(host, port, epoch);
        auto secret = random_bytes(32); Wipe wipe_secret{secret}; auto pub = x25519_public_key(secret);
        auto m2 = p.auth(5, 2, {{6, {1}}, {3, pub}}, epoch);
        const auto& peer_pub = field(m2, 3); require(peer_pub.size() == 32, "Invalid Apple TV ephemeral key");
        auto shared = x25519_shared_secret(secret, peer_pub); Wipe wipe_shared{shared};
        auto key = hkdf_sha512(shared, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info"); Wipe wipe_key{key};
        auto plain = aead(false, key, named_nonce("PV-Msg02"), field(m2, 5)); Wipe wipe_plain{plain};
        auto peer = tlv_decode(plain);
        require(field(peer, 1) == credentials.device_id, "Apple TV identity changed; pair again");
        ed25519_verify(credentials.device_public_key, concat({peer_pub, credentials.device_id, pub}), field(peer, 10));
        auto signed_id = ed25519_sign(credentials.client_secret, concat({pub, credentials.client_id, peer_pub}));
        auto payload = tlv_encode({{1, credentials.client_id}, {10, signed_id}}); Wipe wipe_payload{payload};
        p.auth(6, 4, {{6, {3}}, {5, aead(true, key, named_nonce("PV-Msg03"), payload)}}, epoch);
        p.write_key = hkdf_sha512(shared, "", "ClientEncrypt-main"); p.read_key = hkdf_sha512(shared, "", "ServerEncrypt-main");
        p.command("_systemInfo", {{"_bf", std::uint64_t(0)}, {"_cf", std::uint64_t(512)}, {"_clFl", std::uint64_t(128)},
            {"_i", std::string(credentials.client_id.begin(), credentials.client_id.end())}, {"_idsID", credentials.client_id},
            {"_pubID", public_id(credentials.client_id)}, {"_sf", std::uint64_t(256)}, {"_sv", "170.18"},
            {"model", "HarmonyOS"}, {"name", "Harmony Remote"}}, epoch);
        p.touch_epoch = Clock::now();
        p.command("_touchStart", {{"_height", 1000.0}, {"_width", 1000.0}, {"_tFl", std::uint64_t(0)}}, epoch);
        auto random = random_bytes(4); std::uint64_t sid = 0; for (auto b : random) sid = (sid << 8) | b;
        auto response = p.command("_sessionStart", {{"_srvT", "com.apple.tvremoteservices"}, {"_sid", sid}}, epoch);
        auto content = lookup(response, "_c"); require(content, "Apple TV did not open a remote session");
        auto session_id = content->find("_sid");
        require(session_id && std::holds_alternative<std::uint64_t>(session_id->data), "Apple TV did not open a remote session");
        p.check(epoch); p.connected = true;
    } catch (...) { p.reset(); throw; }
}
void Session::press(std::uint32_t button, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    require((button >= 1 && button <= 7) || button == 14, "Unsupported remote button");
    require(button != 6 || !p.select_held, "Select is already held");
    try {
        p.check(epoch); require(p.connected, "Connect to Apple TV first");
        p.tap(button, epoch); p.check(epoch);
    } catch (...) { p.release(epoch); p.reset(); throw; }
}
void Session::touch(std::uint32_t x, std::uint32_t y, std::uint32_t phase, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    require(x <= 1000 && y <= 1000 && (phase == 1 || phase == 3 || phase == 4), "Invalid touch input");
    require(p.connected, "Connect to Apple TV first");
    require(phase == 1 ? !p.touch_active : p.touch_active, "Invalid touch sequence");
    try {
        p.touch_event(x, y, phase, Clock::now() + p.timeout, epoch);
        p.touch_x = x; p.touch_y = y; p.touch_active = phase != 4; p.check(epoch);
    } catch (...) { p.release(epoch); p.reset(); throw; }
}
void Session::hold_select(bool held, Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    require(p.connected, "Connect to Apple TV first");
    if (p.select_held == held) return;
    try {
        // Track a down before waiting for its acknowledgement. A rejected or
        // truncated reply must still attempt an up before closing the session.
        if (held) p.select_held = true;
        p.command("_hidC", {{"_hBtS", std::uint64_t(held ? 1 : 2)}, {"_hidC", std::uint64_t(6)}}, epoch);
        p.select_held = held; p.check(epoch);
    } catch (...) { p.release(epoch); p.reset(); throw; }
}
void Session::disconnect(Epoch epoch) {
    auto& p = *impl_;
    Impl::OperationGuard operation(p, epoch);
    try { p.check(epoch); p.release(epoch); p.reset(); p.check(epoch); }
    catch (...) { p.reset(); throw; }
}
} // namespace companion
