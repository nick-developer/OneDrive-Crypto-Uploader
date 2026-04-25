cmake_minimum_required(VERSION 3.24)
project(OneDriveCryptoUploader VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object and UI generators
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(ODCU_ENABLE_TESTS "Build tests" ON)

# Core is added explicitly for robustness.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network NetworkAuth)

# OpenSSL is used for AES-256-GCM + PBKDF2.
find_package(OpenSSL REQUIRED)

add_executable(onedrive_crypto_uploader
  src/main.cpp
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
  src/auth/AuthManager.cpp
  src/auth/AuthManager.h
  src/graph/GraphClient.cpp
  src/graph/GraphClient.h
  src/crypto/CryptoEngine.cpp
  src/crypto/CryptoEngine.h
  src/crypto/OdencFormat.cpp
  src/crypto/OdencFormat.h
  src/util/SecureZero.cpp
  src/util/SecureZero.h
  src/util/JsonConfig.cpp
  src/util/JsonConfig.h
)

target_link_libraries(onedrive_crypto_uploader
  PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::NetworkAuth
    OpenSSL::Crypto
)

target_compile_definitions(onedrive_crypto_uploader PRIVATE QT_NO_KEYWORDS)

if(ODCU_ENABLE_TESTS)
  enable_testing()
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

  add_executable(onedrive_crypto_uploader_tests
    tests/test_runner.cpp
    tests/test_crypto.cpp
    tests/test_format.cpp
    tests/test_graph_mock.cpp
    tests/test_gui_smoke.cpp

    src/auth/AuthManager.cpp
    src/auth/AuthManager.h
    src/graph/GraphClient.cpp
    src/graph/GraphClient.h
    src/crypto/CryptoEngine.cpp
    src/crypto/CryptoEngine.h
    src/crypto/OdencFormat.cpp
    src/crypto/OdencFormat.h
    src/util/SecureZero.cpp
    src/util/SecureZero.h
    src/util/JsonConfig.cpp
    src/util/JsonConfig.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
  )

  target_link_libraries(onedrive_crypto_uploader_tests
    PRIVATE
      Qt6::Test
      Qt6::Core
      Qt6::Widgets
      Qt6::Network
      Qt6::NetworkAuth
      OpenSSL::Crypto
  )

  add_test(NAME odcu.unit COMMAND onedrive_crypto_uploader_tests)
endif()
