cmake_minimum_required(VERSION 3.30)
project(kcd2db C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif ()

if (NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This project only supports Windows and MSVC")
endif ()
# 设置UTF-8编码
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

include(FetchContent)

FetchContent_Declare(libmem-config URL "https://raw.githubusercontent.com/rdbo/libmem/config-v1/libmem-config.cmake" DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(libmem-config)
set(CMAKE_PREFIX_PATH "${libmem-config_SOURCE_DIR}" "${CMAKE_PREFIX_PATH}")
set(LIBMEM_DOWNLOAD_VERSION "5.0.4")

find_package(libmem CONFIG REQUIRED)

FetchContent_Declare(sqlite3 GIT_REPOSITORY https://github.com/sjinks/sqlite3-cmake GIT_TAG v3.49.1)
FetchContent_MakeAvailable(sqlite3)

FetchContent_Declare(
        SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp
        GIT_TAG 3.3.1
)
FetchContent_MakeAvailable(SQLiteCpp)

file(GLOB_RECURSE KCD2DB_SOURCES
        "src/*.cpp"
        "src/*.h"
)
add_library(kcd2db SHARED ${KCD2DB_SOURCES})
# Define LM_EXPORT to use libmem as a static library
target_compile_definitions(kcd2db PRIVATE LM_EXPORT)

target_link_libraries(kcd2db PRIVATE libmem::libmem ntdll SQLiteCpp SQLite::SQLite3)

set_target_properties(kcd2db PROPERTIES SUFFIX ".asi" PREFIX "")
