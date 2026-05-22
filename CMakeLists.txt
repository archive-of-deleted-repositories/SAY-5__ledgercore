cmake_minimum_required(VERSION 3.20)
project(ledgercore CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LEDGERCORE_BUILD_TESTS "Build unit/integration tests" ON)
option(LEDGERCORE_BUILD_BENCH "Build bench harness" ON)
option(LEDGERCORE_BUILD_FUZZ "Build libFuzzer targets" OFF)
option(LEDGERCORE_ENABLE_POSTGRES "Build code paths that need libpqxx" OFF)
option(LEDGERCORE_ENABLE_GRPC "Build the gRPC server" OFF)
option(LEDGERCORE_ASAN "Build with AddressSanitizer + UBSan" OFF)
option(LEDGERCORE_TSAN "Build with ThreadSanitizer" OFF)

if(LEDGERCORE_ASAN AND LEDGERCORE_TSAN)
    message(FATAL_ERROR "ASan and TSan are mutually exclusive")
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-deprecated-declarations)

if(LEDGERCORE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()
if(LEDGERCORE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

include(FetchContent)

if(LEDGERCORE_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# Core library (header-heavy, no external deps).
add_library(ledgercore_core
    src/core/transaction.cpp
    src/core/account.cpp
    src/core/ledger.cpp
    src/wal/format.cpp
    src/wal/writer.cpp
    src/wal/replay.cpp
    src/idem/cache.cpp
    src/intake/dispatcher.cpp
    src/workers/worker.cpp
)
target_include_directories(ledgercore_core PUBLIC src)
target_compile_features(ledgercore_core PUBLIC cxx_std_20)

if(LEDGERCORE_ENABLE_POSTGRES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
    target_sources(ledgercore_core PRIVATE src/idem/persist.cpp src/db/postgres.cpp)
    target_link_libraries(ledgercore_core PUBLIC PkgConfig::PQXX)
    target_compile_definitions(ledgercore_core PUBLIC LEDGERCORE_HAVE_POSTGRES=1)
endif()

if(LEDGERCORE_ENABLE_GRPC)
    find_package(Protobuf CONFIG)
    if(NOT Protobuf_FOUND)
        find_package(Protobuf REQUIRED)
    endif()
    find_package(gRPC CONFIG REQUIRED)
    set(PROTO_FILES proto/ledger.proto)
    set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${GEN_DIR})
    add_custom_command(
        OUTPUT ${GEN_DIR}/ledger.pb.cc ${GEN_DIR}/ledger.pb.h
               ${GEN_DIR}/ledger.grpc.pb.cc ${GEN_DIR}/ledger.grpc.pb.h
        COMMAND $<TARGET_FILE:protobuf::protoc>
                --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
                --cpp_out=${GEN_DIR}
                --grpc_out=${GEN_DIR}
                --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILES}
        DEPENDS ${PROTO_FILES}
    )
    add_library(ledgercore_proto
        ${GEN_DIR}/ledger.pb.cc
        ${GEN_DIR}/ledger.grpc.pb.cc
    )
    target_include_directories(ledgercore_proto PUBLIC ${GEN_DIR})
    target_link_libraries(ledgercore_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

    add_library(ledgercore_rpc src/rpc/server.cpp)
    target_link_libraries(ledgercore_rpc PUBLIC ledgercore_core ledgercore_proto)
    target_compile_definitions(ledgercore_rpc PUBLIC LEDGERCORE_HAVE_GRPC=1)

    add_executable(ledgercored src/main.cpp)
    target_link_libraries(ledgercored PRIVATE ledgercore_rpc)
endif()

if(LEDGERCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(LEDGERCORE_BUILD_BENCH)
    add_executable(bench_harness bench/harness.cpp)
    target_link_libraries(bench_harness PRIVATE ledgercore_core)
endif()

if(LEDGERCORE_BUILD_FUZZ)
    add_executable(fuzz_wal tests/fuzz/fuzz_wal.cpp)
    target_link_libraries(fuzz_wal PRIVATE ledgercore_core)
    target_compile_options(fuzz_wal PRIVATE -fsanitize=fuzzer,address,undefined -g)
    target_link_options(fuzz_wal PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
