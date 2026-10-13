cmake_minimum_required(VERSION 3.16)
project(companion_harmony LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
  message(FATAL_ERROR "Use the HarmonyOS SDK native CMake toolchain, not Android NDK or host CMake.")
endif()
if(NOT OHOS_ARCH STREQUAL "arm64-v8a")
  message(FATAL_ERROR "This POC is configured for HarmonyOS arm64-v8a only.")
endif()
set(COMPANION_HARMONY_OPENSSL_ROOT "$ENV{COMPANION_HARMONY_OPENSSL_ROOT}" CACHE PATH "Locally prepared OpenSSL 3.x for aarch64-linux-ohos")
if(NOT EXISTS "${COMPANION_HARMONY_OPENSSL_ROOT}/include/openssl/evp.h" OR
   NOT EXISTS "${COMPANION_HARMONY_OPENSSL_ROOT}/lib/libcrypto.a")
  message(FATAL_ERROR "Set COMPANION_HARMONY_OPENSSL_ROOT to an OpenSSL 3.x HarmonyOS arm64 install with include/openssl and lib/libcrypto.a. No dependencies are downloaded.")
endif()
# Require an explicit target marker as an additional guard against accidental host/Android libs.
# The marker records provenance; it does not replace inspecting ELF members and cross-link testing.
if(NOT EXISTS "${COMPANION_HARMONY_OPENSSL_ROOT}/HARMONY_OPENSSL_TARGET")
  message(FATAL_ERROR "Missing HARMONY_OPENSSL_TARGET: verify this static archive was built with the HarmonyOS SDK, then write aarch64-linux-ohos into that marker file.")
endif()
file(READ "${COMPANION_HARMONY_OPENSSL_ROOT}/HARMONY_OPENSSL_TARGET" COMPANION_OPENSSL_TARGET)
string(STRIP "${COMPANION_OPENSSL_TARGET}" COMPANION_OPENSSL_TARGET)
if(NOT COMPANION_OPENSSL_TARGET STREQUAL "aarch64-linux-ohos")
  message(FATAL_ERROR "OpenSSL target marker must be aarch64-linux-ohos.")
endif()
get_filename_component(COMPANION_NATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native" ABSOLUTE)
add_library(harmony_crypto STATIC IMPORTED)
set_target_properties(harmony_crypto PROPERTIES IMPORTED_LOCATION "${COMPANION_HARMONY_OPENSSL_ROOT}/lib/libcrypto.a" INTERFACE_INCLUDE_DIRECTORIES "${COMPANION_HARMONY_OPENSSL_ROOT}/include")
add_library(companion SHARED
  "${COMPANION_NATIVE_ROOT}/src/crypto.cpp"
  "${COMPANION_NATIVE_ROOT}/src/codec.cpp"
  "${COMPANION_NATIVE_ROOT}/src/session.cpp"
  "${COMPANION_NATIVE_ROOT}/bridge/napi.cpp")
target_include_directories(companion PRIVATE "${COMPANION_NATIVE_ROOT}/include")
target_compile_options(companion PRIVATE -Wall -Wextra -Wpedantic -fstack-protector-strong)
target_link_options(companion PRIVATE -Wl,-z,relro,-z,now -Wl,--no-undefined)
target_link_libraries(companion PRIVATE harmony_crypto libace_napi.z.so)
