// SPDX-License-Identifier: MIT
#include "companion/codec.hpp"
#include "companion/crypto.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace companion;
namespace {
Bytes hex(const std::string& text) {
  if (text.size() % 2) throw std::runtime_error("Invalid test hex");
  Bytes result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
  return result;
}
void expect(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); }
void reject(const std::function<void()>& action, const char* label) {
  try { action(); } catch (const std::exception&) { return; }
  throw std::runtime_error(label);
}

void test_ed25519() {
  // RFC8032 section 7.1, TEST 1; also shared with Kotlin ProtocolTest.
  const auto seed = hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  const auto pub = hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  auto signature = hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  expect(ed25519_public_key(seed) == pub, "RFC8032 public key");
  expect(ed25519_sign(seed, {}) == signature, "RFC8032 signature");
  ed25519_verify(pub, {}, signature);
  reject([&] { ed25519_verify(pub, Bytes{1}, signature); }, "Accepted modified signed message");
  signature[20] ^= 1;
  reject([&] { ed25519_verify(pub, {}, signature); }, "Accepted forged signature");
  reject([&] { ed25519_public_key(Bytes(31)); }, "Accepted short Ed25519 seed");
  reject([&] { ed25519_verify(pub, {}, Bytes(63)); }, "Accepted short signature");
}

void test_chacha() {
  // RFC8439 section 2.8.2 AEAD_CHACHA20_POLY1305.
  const auto key = hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
  const auto nonce = hex("070000004041424344454647");
  const auto aad = hex("50515253c0c1c2c3c4c5c6c7");
  const auto plain = bytes("Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.");
  const auto cipher = hex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691");
  expect(aead(true, key, nonce, plain, aad) == cipher, "RFC8439 encrypt");
  expect(aead(false, key, nonce, cipher, aad) == plain, "RFC8439 decrypt");
  reject([&] { aead(false, key, nonce, cipher, Bytes{0}); }, "Accepted tampered AAD");
  auto corrupt = cipher; corrupt.back() ^= 1;
  reject([&] { aead(false, key, nonce, corrupt, aad); }, "Accepted tampered tag");
  reject([&] { aead(false, key, nonce, Bytes(15)); }, "Accepted truncated tag");
  reject([&] { aead(true, key, Bytes(8), plain); }, "Accepted bad nonce size");
  const auto empty = aead(true, key, nonce, {});
  expect(empty.size() == 16 && aead(false, key, nonce, empty).empty(), "Empty AEAD payload");
  expect(counter_nonce(1) == hex("010000000000000000000000"), "Counter endian");
  expect(counter_nonce(0x8877665544332211ULL) == hex("112233445566778800000000"), "Counter high bits");
  expect(named_nonce("PS-Msg05") == hex("0000000050532d4d73673035"), "Named nonce");
  reject([] { named_nonce("short"); }, "Accepted short named nonce");
}

void test_hkdf_and_x25519() {
  // HKDF-SHA512 expected bytes generated independently with Python hmac/hashlib:
  // PRK=HMAC-SHA512(bytes(range(13)), b'\x0b'*22)
  // OKM=HMAC-SHA512(PRK, bytes(range(0xf0,0xfa))+b'\x01')[:42]
  const auto salt_bytes = hex("000102030405060708090a0b0c");
  const auto info_bytes = hex("f0f1f2f3f4f5f6f7f8f9");
  expect(hkdf_sha512(Bytes(22, 0x0b), std::string(salt_bytes.begin(), salt_bytes.end()),
                    std::string(info_bytes.begin(), info_bytes.end()), 42) ==
         hex("832390086cda71fb47625bb5ceb168e4c8e26a1a16ed34d9fc7fe92c1481579338da362cb8d9f925d7cb"), "HKDF-SHA512 expected bytes");
  expect(sha512({bytes("abc")}) == hex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"), "SHA512 abc");
  reject([] { hkdf_sha512({}, "", "", 16321); }, "Accepted HKDF limit overflow");
  // RFC7748 section 6.1.
  const auto alice = hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
  const auto bob = hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
  const auto alice_pub = hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
  const auto bob_pub = hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
  const auto shared = hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
  expect(x25519_public_key(alice) == alice_pub && x25519_public_key(bob) == bob_pub, "RFC7748 public keys");
  expect(x25519_shared_secret(alice, bob_pub) == shared, "RFC7748 shared secret");
  expect(x25519_shared_secret(bob, alice_pub) == shared, "RFC7748 reciprocal secret");
  reject([&] { x25519_shared_secret(alice, Bytes(32)); }, "Accepted all-zero X25519 peer");
  auto secret = random_bytes(32); expect(secret.size() == 32, "Random length");
  wipe(secret); expect(secret.empty(), "Wipe clears secret");
}

void test_srp() {
  // Independent Python pow/hashlib fixture, not computed by this implementation.
  // RFC5054 3072-bit N (RFC3526 group 15), g=5; I='Pair-Setup', P='1234'.
  // a below, b=e487cb59d31ac550471e81f00f6928e01dda08e974a004f49e61f5d105284d20.
  // x=H(s,H(I+':'+P)); k=H(PAD(N),PAD(g)); v=g^x mod N;
  // B=(k*v+g^b)%N; u=H(PAD(A),PAD(B)); S=(A*v^u)^b mod N;
  // K=H(unsigned(S)); M=H(H(N) xor H(g),H(I),s,A,B,K); M2=H(A,M,K).
  const auto salt = hex("beb25379d1a8581eb5a727673a2441ee");
  const auto a = hex("60975527035cf2ad1989806f0407210bc81edc04e2762a56afd529ddda2d4393");
  const auto aa = hex(
    "fab6f5d2615d1e323512e7991cc37443f487da604ca8c9230fcb04e541dce6280b27ca4680b0374f179dc3bdc7553fe62459798c701ad864a91390a28c93b644"
    "adbf9c00745b942b79f9012a21b9b78782319d83a1f8362866fbd6f46bfc0ddb2e1ab6e4b45a9906b82e37f05d6f97f6a3eb6e182079759c4f6847837b62321a"
    "c1b4fa68641fcb4bb98dd697a0c73641385f4bab25b793584cc39fc8d48d4bd867a9a3c10f8ea12170268e34fe3bbe6ff89998d60da2f3e4283cbec1393d52af724"
    "a57230c604e9fbce583d7613e6bffd67596ad121a8707eec46944957033686a155f644d5c5863b48f61bdbf19a53eab6dad0a186b8c152e5f5d8cad4b0ef8aa4ea5"
    "008834c3cd342e5e0f167ad04592cd8bd279639398ef9e114dfaaab919e14e850989224ddd98576d79385d2210902e9f9b1f2d86cfa47ee244635465f71058421a0184"
    "be51dd10cc9d079e6f1604e7aa9b7cf7883c7d4ce12b06ebe16081e23f27a231d18432d7d1bb55c28ae21ffcf005f57528d15a88881bb3bbb7fe");
  const auto b = hex(
    "4e4547a9f93ccaea76dd793a6c0ffce0385d8dfe338ace8704689ff238cda958ef7795e63c38880da7426c5d2678878a6337ce7dd608ce4cbf3534d26d9c43b08"
    "8ee474e96242533a942fde8274fc1f5c6b496fab89a0e51bcc98d7544ed0192a6d749fc9e16240d7b6523e6b6670e355280e91e67a96b0fef40f5041f6eaec7de"
    "0051bb9b4ea504e915b1c469351ed0f06e5fb3f1fa2dbe57296e8ba77823b96cb588f525fa6eb53ace342db250ac4ce8ec9f9ea4a4745f77a36bf25e48f346a7f0"
    "99d35b632bb71d7b2c9fe2da640819925677af7272044ba0995da9029823c5e6562aef6f745047d775c2f48ebcc1755315b7040840f341f898d3797f89d8a23ea88f"
    "73ce211711eefa702a2f8fea8ec9ba8542264a2869bc4e6ef2958067a817e5c3511baa4d4d57f9582fc3a45d1072244982649b53cde7a618e3bb7df27ced66c681a8"
    "cc9b12821edd53e099140e69c4f9c1fc7dacae189f96103b9f4e0a93ceb15b9eb628802c7987f9f47e7161d1f2054abe5e5bd48cb3d0901578f8");
  Srp3072 srp("1234", salt, b, a);
  expect(srp.public_key == aa, "Independent SRP A");
  expect(srp.session_key == hex("a0b5144ff95381744fe8c01561df3db6d4175c9e3b3f790075223cd41ab3bc95b635e096bf221b0c04d3e27dde06389a500c87acdff95de437efcbaeadd7fcf1"), "Independent SRP session key");
  expect(srp.proof == hex("71eeb1460f9a3947e7a66e87a80e4d7e2ab3a2685f2a789e802475192aec98c5a0b973b71bb28a5b1d7ee710036c3dd9343968e7dca7a617cb21d02fd8523127"), "Independent SRP client proof");
  const auto proof = hex("291ef1c9f483c4e9a5a9afcc2dd2c013fad2bab2b21339ed4a77d90c55299b25a8b9cf83afaa3d19c27217a2cde36efdaa947c20a79c7428571fffb00435de93");
  expect(srp.expected_server_proof == proof, "Independent SRP server proof"); srp.verify(proof);
  reject([&] { srp.verify(Bytes(64)); }, "Accepted incorrect server proof");
  reject([&] { Srp3072 invalid("1234", salt, Bytes{0}); }, "Accepted zero SRP B");
  const auto n = hex(
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404ddef9519b3cd3a431b302b0a6df25f14374f"
    "e1356d6d51c245e485b576625e7ec6f44c42e9a637ed6b0bff5cb6f406b7edee386bfb5a899fa5ae9f24117c4b1fe649286651ece45b3dc2007cb8a163bf0598"
    "da48361c55d39a69163fa8fd24cf5f83655d23dca3ad961c62f356208552bb9ed529077096966d670c354e4abc9804f1746c08ca18217c32905e462e36ce3be39e77"
    "2c180e86039b2783a2ec07a28fb5c55df06f4c52c9de2bcbf6955817183995497cea956ae515d2261898fa051015728e5a8aaac42dad33170d04507a33a85521abdf"
    "1cba64ecfb850458dbef0a8aea71575d060c7db3970f85a6e1e4c7abf5ae8cdb0933d71e8c94e04a25619dcee3d2261ad2ee6bf12ffa06d98a0864d87602733ec8"
    "6a64521f2b18177b200cbbe117577a615d6c770988c0bad946e208e24fa074e5ab3143db5bfce0fd108e4b82d120a93ad2caffffffffffffffff");
  reject([&] { Srp3072 invalid("1234", salt, n); }, "Accepted zero-mod-N SRP B");
  reject([&] { Srp3072 invalid("1234", salt, Bytes(385, 255)); }, "Accepted oversized SRP B");
  Srp3072 random_a("1234", salt, b); Srp3072 random_b("1234", salt, b);
  expect(random_a.public_key != random_b.public_key, "SRP production exponent is random");
}

void test_opack() {
  const Value value(Dict{{"_t", 2}, {"_x", 1}, {"_i", "_hidC"}});
  // Byte-exact fixture emitted by existing Kotlin ProtocolTest. C++ dicts are
  // sorted, so encoding has a deterministic equivalent order of its own.
  expect(opack_decode(hex("e3425f740a425f7809425f69455f68696443")) == value, "Kotlin OPACK fixture decode");
  expect(opack_encode(value) == hex("e3425f69455f68696443425f740a425f7809"), "Canonical OPACK fixture encode");
  expect(opack_decode(hex("d2446e616d65a0")) == Value(Array{"name", "name"}), "OPACK reference");
  expect(opack_decode(hex("d5417841784179a1a0")) == Value(Array{"x", "x", "y", "y", "x"}), "OPACK deduplicated reference table");
  expect(opack_encode(Value(1000.0)) == hex("360000000000408f40"), "OPACK double wire bytes");
  expect(opack_decode(hex("350000803f")) == Value(1.0), "OPACK float");
  expect(opack_decode(hex("d4350000803f36000000000000f03f4178a2")) == Value(Array{1.0, 1.0, "x", "x"}), "OPACK float-width reference indices");
  expect(opack_decode(opack_encode(Value(std::numeric_limits<std::uint64_t>::max()))).uint() == std::numeric_limits<std::uint64_t>::max(), "OPACK uint64 preserved");
  for (const auto n : {0ULL, 39ULL, 40ULL, 255ULL, 256ULL, 65535ULL, 65536ULL, 0xffffffffULL, 0x100000000ULL})
    expect(opack_decode(opack_encode(Value(n))).uint() == n, "OPACK integer boundaries");
  for (const auto size : {0U, 32U, 33U, 255U, 256U, 65535U, 65536U}) {
    Bytes binary(size); for (std::size_t i = 0; i < size; ++i) binary[i] = static_cast<std::uint8_t>(i);
    expect(opack_decode(opack_encode(Value(binary))).blob() == binary, "OPACK binary length boundary");
    const std::string text(size, 'x');
    expect(opack_decode(opack_encode(Value(text))).string() == text, "OPACK string length boundary");
  }
  Array items(20, Value(true)); expect(opack_decode(opack_encode(Value(items))) == Value(items), "OPACK indefinite array");
  Dict dict; for (unsigned i = 0; i < 20; ++i) dict.emplace(std::to_string(i), Value(i));
  expect(opack_decode(opack_encode(Value(dict))) == Value(dict), "OPACK indefinite dictionary");
  for (const auto* malformed : {"", "62ffff", "a0", "ef", "e14178", "0808", "df", "03", "e10808", "d24178a1", "94ffffffffffffffff", "e2417808417809"})
    reject([&] { opack_decode(hex(malformed)); }, "Accepted malformed OPACK");
  Bytes nested(48, 0xd1); nested.push_back(8);
  reject([&] { opack_decode(nested); }, "Accepted excessive OPACK nesting");
  reject([] { opack_decode(Bytes(kMaxCodecBytes + 1)); }, "Accepted oversized OPACK input");
  reject([] { opack_encode(Value(Bytes(kMaxCodecBytes))); }, "Accepted oversized OPACK output");
  Bytes amplification{0xdf}; const auto large = opack_encode(Value(Bytes(65536)));
  amplification.insert(amplification.end(), large.begin(), large.end());
  amplification.insert(amplification.end(), 16, 0xa0); amplification.push_back(3);
  reject([&] { opack_decode(amplification); }, "Accepted OPACK reference amplification");
  reject([] { Value(-1); }, "Accepted negative OPACK integer");
  expect(value.find("_x") && value.find("_x")->uint() == 1 && !value.find("missing"), "Dictionary lookup");
}

void test_tlv() {
  Bytes binary(600); for (std::size_t i = 0; i < binary.size(); ++i) binary[i] = static_cast<std::uint8_t>(i);
  const auto encoded = tlv_encode({{3, binary}, {6, Bytes{2}}, {7, {}}});
  expect(encoded[0] == 3 && encoded[1] == 255 && encoded[257] == 3 && encoded[258] == 255 && encoded[514] == 3 && encoded[515] == 90, "TLV fragmentation headers");
  const auto decoded = tlv_decode(encoded);
  expect(decoded.at(3) == binary && decoded.at(6) == Bytes{2} && decoded.at(7).empty(), "TLV fragmented round trip");
  expect(tlv_decode(hex("030101030102")).at(3) == Bytes({1, 2}), "TLV repeated fields");
  reject([] { tlv_decode(hex("03ff00")); }, "Accepted truncated TLV value");
  reject([] { tlv_decode(hex("03")); }, "Accepted truncated TLV header");
  reject([] { tlv_encode({{3, Bytes(kMaxCodecBytes)}}); }, "Accepted oversized TLV output");
}

void test_parser_smoke() {
  // Reproducible malformed-input smoke coverage, also run under ASan/UBSan.
  std::mt19937 rng(0xc0face);
  for (unsigned i = 0; i < 10000; ++i) {
    Bytes input(rng() % 256);
    for (auto& b : input) b = static_cast<std::uint8_t>(rng());
    try { (void)opack_decode(input); } catch (const std::invalid_argument&) {}
    try { (void)tlv_decode(input); } catch (const std::invalid_argument&) {}
  }
}
}  // namespace

int main() {
  try {
    test_ed25519(); test_chacha(); test_hkdf_and_x25519(); test_srp(); test_opack(); test_tlv(); test_parser_smoke();
    std::cout << "crypto_codec_test: all known vectors, bounds, and rejection cases passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "crypto_codec_test failed: " << error.what() << '\n'; return 1;
  }
}
