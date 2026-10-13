// SPDX-License-Identifier: MIT
#include "companion/session.hpp"
#ifdef COMPANION_NODE_HOST
#include <node_api.h>
#else
#include <napi/native_api.h>
#include <node_api.h>
#endif

#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using companion::Bytes;
using companion::Credentials;
using companion::Session;
constexpr const char* kVersion = "0.1.0-companion-poc";
constexpr std::size_t kMaxQueued = 64;

void checked(napi_status status) {
    if (status != napi_ok) throw std::runtime_error("Native bridge API failed");
}
void erase(std::string& value) noexcept {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (std::size_t i = 0; i < value.size(); ++i) data[i] = 0;
    value.clear();
}
struct State;
enum class Operation { Begin, Finish, Connect, Press, Disconnect };
struct Job {
    Operation operation;
    Session::Epoch epoch = 0;
    std::string host, pin, error, code;
    std::uint16_t port = 0;
    std::uint32_t button = 0;
    Bytes credentials, result;
    napi_deferred deferred = nullptr;
    napi_async_work work = nullptr;
    std::shared_ptr<State> state;
    ~Job() { erase(pin); companion::wipe(credentials); companion::wipe(result); }
};
struct State {
    Session session;
    std::atomic<bool> closing{false};
    std::deque<std::unique_ptr<Job>> queued; // Accessed only on the JS thread.
    Job* active = nullptr;
};
struct Holder { std::shared_ptr<State> state = std::make_shared<State>(); };

std::string error_code(const std::string& text) {
    if (text.find("cancelled") != std::string::npos) return "CANCELLED";
    if (text.find("identity changed") != std::string::npos) return "IDENTITY_CHANGED";
    if (text.find("timed out") != std::string::npos) return "TIMEOUT";
    if (text.find("Connect to Apple TV first") != std::string::npos || text.find("Start pairing first") != std::string::npos) return "NOT_CONNECTED";
    if (text.find("saved credentials") != std::string::npos || text.find("address") != std::string::npos || text.find("IPv6 scope") != std::string::npos || text.find("four-digit PIN") != std::string::npos || text.find("Unsupported remote button") != std::string::npos) return "INVALID_INPUT";
    if (text.find("identity") != std::string::npos || text.find("signature") != std::string::npos || text.find("authentication") != std::string::npos || text.find("pairing") != std::string::npos || text.find("proof") != std::string::npos || text.find("AEAD") != std::string::npos) return "AUTH_FAILED";
    if (text.find("disconnected") != std::string::npos || text.find("connect to Apple TV") != std::string::npos || text.find("socket") != std::string::npos || text.find("send failed") != std::string::npos) return "DISCONNECTED";
    return "PROTOCOL_ERROR";
}
// Only locally authored messages cross into JavaScript; peer strings are never exposed.
const char* safe_message(const std::string& code) {
    if (code == "CANCELLED") return "Operation cancelled";
    if (code == "INVALID_INPUT") return "Invalid address, port, PIN, credentials, or button";
    if (code == "AUTH_FAILED") return "Apple TV authentication failed; pair again if needed";
    if (code == "IDENTITY_CHANGED") return "Apple TV identity changed; pair again";
    if (code == "TIMEOUT") return "Apple TV response timed out";
    if (code == "DISCONNECTED") return "Apple TV connection failed or closed";
    if (code == "NOT_CONNECTED") return "Start pairing or connect to Apple TV first";
    if (code == "BUSY") return "Too many pending remote operations";
    if (code == "INTERNAL_ERROR") return "Native bridge operation failed";
    return "Invalid or rejected Companion response";
}
void reject(napi_env env, napi_deferred deferred, const std::string& code) {
    napi_value message, error, code_value;
    checked(napi_create_string_utf8(env, safe_message(code), NAPI_AUTO_LENGTH, &message));
    checked(napi_create_string_utf8(env, code.c_str(), code.size(), &code_value));
    checked(napi_create_error(env, code_value, message, &error));
    checked(napi_reject_deferred(env, deferred, error));
}
void start_next(napi_env env, const std::shared_ptr<State>& state);
void execute(napi_env, void* data) noexcept {
    auto& job = *static_cast<Job*>(data);
    try {
        auto& session = job.state->session;
        if (job.state->closing || job.epoch != session.epoch()) throw std::runtime_error("Operation cancelled");
        switch (job.operation) {
            case Operation::Begin: session.begin_pairing(job.host, job.port, job.epoch); break;
            case Operation::Finish: job.result = session.finish_pairing(job.pin, job.epoch).encode(); break;
            case Operation::Connect: {
                auto credentials = Credentials::decode(job.credentials);
                companion::wipe(job.credentials);
                session.connect(job.host, job.port, credentials, job.epoch); break;
            }
            case Operation::Press: session.press(job.button, job.epoch); break;
            case Operation::Disconnect: session.disconnect(job.epoch); break;
        }
    } catch (const std::exception& error) { job.code = error_code(error.what()); }
    catch (...) { job.code = "INTERNAL_ERROR"; }
    erase(job.pin); companion::wipe(job.credentials);
}
void complete(napi_env env, napi_status status, void* data) noexcept {
    std::unique_ptr<Job> job(static_cast<Job*>(data));
    auto state = job->state; state->active = nullptr;
    if (!env || state->closing.load()) return;
    try {
        checked(napi_delete_async_work(env, job->work)); job->work = nullptr;
        if (job->epoch != state->session.epoch() || status == napi_cancelled) job->code = "CANCELLED";
        else if (status != napi_ok) job->code = "INTERNAL_ERROR";
        if (!job->code.empty()) reject(env, job->deferred, job->code);
        else {
            napi_value value;
            if (job->operation == Operation::Finish) {
                void* output = nullptr;
                checked(napi_create_arraybuffer(env, job->result.size(), &output, &value));
                std::memcpy(output, job->result.data(), job->result.size());
            } else checked(napi_get_undefined(env, &value));
            checked(napi_resolve_deferred(env, job->deferred, value));
        }
        start_next(env, state);
    } catch (...) {
        // A pending JS exception / torn-down environment cannot safely be re-entered.
        state->closing = true; state->session.cancel(); state->queued.clear();
    }
}
void start_next(napi_env env, const std::shared_ptr<State>& state) {
    while (!state->closing && !state->active && !state->queued.empty()) {
        auto job = std::move(state->queued.front()); state->queued.pop_front();
        if (job->epoch != state->session.epoch()) { reject(env, job->deferred, "CANCELLED"); continue; }
        napi_value name; checked(napi_create_string_utf8(env, "CompanionOperation", NAPI_AUTO_LENGTH, &name));
        job->state = state;
        auto create = napi_create_async_work(env, nullptr, name, execute, complete, job.get(), &job->work);
        if (create != napi_ok) { reject(env, job->deferred, "INTERNAL_ERROR"); continue; }
        if (napi_queue_async_work(env, job->work) != napi_ok) {
            checked(napi_delete_async_work(env, job->work)); reject(env, job->deferred, "INTERNAL_ERROR"); continue;
        }
        state->active = job.release();
    }
}
std::string string_arg(napi_env env, napi_value value, std::size_t max) {
    napi_valuetype type; checked(napi_typeof(env, value, &type));
    if (type != napi_string) throw std::invalid_argument("string");
    std::size_t size = 0; checked(napi_get_value_string_utf8(env, value, nullptr, 0, &size));
    if (!size || size > max) throw std::invalid_argument("string size");
    // Use a byte vector so the nul terminator is within allocated, writable storage.
    Bytes buffer(size + 1); std::size_t actual = 0;
    checked(napi_get_value_string_utf8(env, value, reinterpret_cast<char*>(buffer.data()), buffer.size(), &actual));
    std::string result(reinterpret_cast<char*>(buffer.data()), actual); companion::wipe(buffer);
    if (actual != size || result.find('\0') != std::string::npos) { erase(result); throw std::invalid_argument("string content"); }
    return result;
}
std::uint32_t integer_arg(napi_env env, napi_value value, std::uint32_t max) {
    napi_valuetype type; checked(napi_typeof(env, value, &type));
    if (type != napi_number) throw std::invalid_argument("number");
    double number; checked(napi_get_value_double(env, value, &number));
    if (!std::isfinite(number) || number < 1 || number > max || std::floor(number) != number) throw std::invalid_argument("integer");
    return static_cast<std::uint32_t>(number);
}
napi_value enqueue(napi_env env, napi_callback_info info, Operation operation) noexcept {
    napi_deferred deferred = nullptr; napi_value promise = nullptr;
    try {
        checked(napi_create_promise(env, &deferred, &promise));
        napi_value argv[8]; std::size_t argc = 8; void* data = nullptr;
        checked(napi_get_cb_info(env, info, &argc, argv, nullptr, &data));
        auto state = static_cast<Holder*>(data)->state;
        auto job = std::make_unique<Job>(); job->operation = operation; job->deferred = deferred;
        job->epoch = state->session.epoch();
        std::size_t expected = operation == Operation::Begin ? 2 : operation == Operation::Connect ? 3 : operation == Operation::Disconnect ? 0 : 1;
        if (argc != expected) throw std::invalid_argument("argument count");
        if (operation == Operation::Begin || operation == Operation::Connect) {
            job->host = string_arg(env, argv[0], 128); job->port = static_cast<std::uint16_t>(integer_arg(env, argv[1], 65535));
        }
        if (operation == Operation::Finish) {
            job->pin = string_arg(env, argv[0], 4);
            if (job->pin.size() != 4 || job->pin.find_first_not_of("0123456789") != std::string::npos) throw std::invalid_argument("PIN");
        }
        if (operation == Operation::Connect) {
            bool is_buffer = false; checked(napi_is_arraybuffer(env, argv[2], &is_buffer));
            if (!is_buffer) throw std::invalid_argument("credentials buffer");
            void* input = nullptr; std::size_t size = 0; checked(napi_get_arraybuffer_info(env, argv[2], &input, &size));
            if (!input || size < 4 || size > 332) throw std::invalid_argument("credentials size");
            auto* begin = static_cast<std::uint8_t*>(input); job->credentials.assign(begin, begin + size);
        }
        if (operation == Operation::Press) {
            job->button = integer_arg(env, argv[0], 14);
            if (job->button > 7 && job->button != 14) throw std::invalid_argument("button");
        }
        if (state->closing) { reject(env, deferred, "CANCELLED"); return promise; }
        if (state->queued.size() >= kMaxQueued) { reject(env, deferred, "BUSY"); return promise; }
        state->queued.push_back(std::move(job)); start_next(env, state); return promise;
    } catch (const std::invalid_argument&) {
        try { if (deferred) { reject(env, deferred, "INVALID_INPUT"); return promise; } } catch (...) { }
    } catch (...) {
        try { if (deferred) { reject(env, deferred, "INTERNAL_ERROR"); return promise; } } catch (...) { }
    }
    napi_throw_error(env, "INTERNAL_ERROR", "Native bridge API failed"); return nullptr;
}
napi_value begin(napi_env env, napi_callback_info info) { return enqueue(env, info, Operation::Begin); }
napi_value finish(napi_env env, napi_callback_info info) { return enqueue(env, info, Operation::Finish); }
napi_value connect(napi_env env, napi_callback_info info) { return enqueue(env, info, Operation::Connect); }
napi_value press(napi_env env, napi_callback_info info) { return enqueue(env, info, Operation::Press); }
napi_value disconnect(napi_env env, napi_callback_info info) { return enqueue(env, info, Operation::Disconnect); }
napi_value cancel(napi_env env, napi_callback_info info) noexcept {
    try {
        std::size_t argc = 0; void* data = nullptr; checked(napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data));
        auto state = static_cast<Holder*>(data)->state; state->session.cancel();
        while (!state->queued.empty()) {
            auto job = std::move(state->queued.front()); state->queued.pop_front(); reject(env, job->deferred, "CANCELLED");
        }
        napi_value result; checked(napi_get_undefined(env, &result)); return result;
    } catch (...) { napi_throw_error(env, "INTERNAL_ERROR", "Native cancellation failed"); return nullptr; }
}
napi_value version(napi_env env, napi_callback_info) noexcept {
    napi_value result;
    if (napi_create_string_utf8(env, kVersion, NAPI_AUTO_LENGTH, &result) != napi_ok) return nullptr;
    return result;
}
void cleanup(void* data) {
    std::unique_ptr<Holder> holder(static_cast<Holder*>(data));
    holder->state->closing = true; holder->state->session.cancel(); holder->state->queued.clear();
}
napi_value Init(napi_env env, napi_value exports) {
    try {
        auto holder = std::make_unique<Holder>();
        napi_property_descriptor descriptors[] = {
            {"beginPairing", nullptr, begin, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"finishPairing", nullptr, finish, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"connect", nullptr, connect, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"press", nullptr, press, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"disconnect", nullptr, disconnect, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"cancel", nullptr, cancel, nullptr, nullptr, nullptr, napi_default, holder.get()},
            {"version", nullptr, version, nullptr, nullptr, nullptr, napi_default, holder.get()}
        };
        checked(napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors));
        checked(napi_add_env_cleanup_hook(env, cleanup, holder.get())); holder.release(); return exports;
    } catch (...) { napi_throw_error(env, "INTERNAL_ERROR", "Could not initialize Companion bridge"); return nullptr; }
}
} // namespace
#ifdef COMPANION_NODE_HOST
NAPI_MODULE(companion, Init)
#else
static napi_module companion_module = {
    1, 0, nullptr, Init, "companion", nullptr, {0}
};
extern "C" __attribute__((constructor)) void RegisterCompanionModule() { napi_module_register(&companion_module); }
#endif
