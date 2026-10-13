// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace companion {
using Bytes = std::vector<std::uint8_t>;

// Non-owning call-scoped view avoids temporary copies of secret inputs in
// sha512({bytes1, bytes2}) and concat({bytes1, bytes2}). Never retain a view.
struct ByteView {
  const std::uint8_t* data;
  std::size_t size;
  ByteView(const Bytes& value) noexcept : data(value.data()), size(value.size()) {}
};

Bytes bytes(const std::string& text);
Bytes concat(std::initializer_list<ByteView> parts);
void wipe(Bytes& value) noexcept;
Bytes random_bytes(std::size_t count);
Bytes sha512(std::initializer_list<ByteView> parts);
Bytes hkdf_sha512(const Bytes& key, const std::string& salt,
                  const std::string& info, std::size_t length = 32);
// AEAD returns ciphertext || 16-byte tag, or verified plaintext. Throws on failure.
Bytes aead(bool encrypt, const Bytes& key, const Bytes& nonce,
           const Bytes& data, const Bytes& aad = {});
Bytes named_nonce(const std::string& name);
// Companion uses the LE64 counter followed by four zero bytes.
Bytes counter_nonce(std::uint64_t counter);
Bytes ed25519_public_key(const Bytes& seed);
Bytes ed25519_sign(const Bytes& seed, const Bytes& data);
void ed25519_verify(const Bytes& public_key, const Bytes& data,
                    const Bytes& signature);
Bytes x25519_public_key(const Bytes& private_key);
Bytes x25519_shared_secret(const Bytes& private_key, const Bytes& peer_public_key);

// HAP Pair-Setup SRP-6a: RFC5054 3072-bit group, SHA-512, HAP proofs.
// Adapted from the repository's Kotlin implementation; protocol attribution is
// retained in protocol/LICENSE-pyatv.txt and THIRD_PARTY_NOTICES.md.
class Srp3072 {
 public:
  Srp3072(const std::string& pin, const Bytes& salt, const Bytes& server_public);
  // Deterministic known-answer testing only. Normal callers use the overload above.
  Srp3072(const std::string& pin, const Bytes& salt, const Bytes& server_public,
          const Bytes& test_private_exponent);
  ~Srp3072();
  Srp3072(const Srp3072&) = delete;
  Srp3072& operator=(const Srp3072&) = delete;
  Srp3072(Srp3072&&) = delete;
  Srp3072& operator=(Srp3072&&) = delete;
  void verify(const Bytes& server_proof) const;

  Bytes public_key;
  Bytes proof;
  Bytes expected_server_proof;
  Bytes session_key;
};
}  // namespace companion
