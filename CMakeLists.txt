cmake_minimum_required(VERSION 3.20)

project(PixelWarRemake
    VERSION 0.1.0
    DESCRIPTION "High-performance C++ pixel map web server"
    LANGUAGES CXX
)

option(PIXELWAR_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_library(pixelwar_core
    src/config/ServerConfig.cpp
    src/controllers/ApiController.cpp
    src/controllers/StaticController.cpp
    src/http/HttpServer.cpp
    src/http/HttpTypes.cpp
    src/http/Router.cpp
    src/models/User.cpp
    src/security/PasswordHasher.cpp
    src/security/RateLimiter.cpp
    src/security/SessionManager.cpp
    src/storage/PixelMap.cpp
    src/storage/UserStore.cpp
    src/utils/Base64.cpp
    src/utils/Json.cpp
    src/utils/Random.cpp
    src/utils/HttpsClient.cpp
    src/utils/Sha256.cpp
    src/utils/ThreadPool.cpp
)

set_target_properties(pixelwar_core PROPERTIES CXX_SCAN_FOR_MODULES OFF)

target_include_directories(pixelwar_core PUBLIC include)

if (WIN32)
    target_link_libraries(pixelwar_core PUBLIC ws2_32 winhttp)
endif()

if (MSVC)
    target_compile_options(pixelwar_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pixelwar_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pixelwar_server src/main.cpp)
set_target_properties(pixelwar_server PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(pixelwar_server PRIVATE pixelwar_core)

if (PIXELWAR_BUILD_TESTS)
    enable_testing()

    find_package(Catch2 3 QUIET)
    add_executable(pixelwar_tests tests/test_core.cpp)
    set_target_properties(pixelwar_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(pixelwar_tests PRIVATE pixelwar_core)
    target_compile_definitions(pixelwar_tests PRIVATE PIXELWAR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    if (Catch2_FOUND)
        target_link_libraries(pixelwar_tests PRIVATE Catch2::Catch2WithMain)
        target_compile_definitions(pixelwar_tests PRIVATE PIXELWAR_HAS_CATCH2=1)
    else()
        target_include_directories(pixelwar_tests PRIVATE tests)
    endif()

    add_test(NAME pixelwar_tests COMMAND pixelwar_tests)
endif()
