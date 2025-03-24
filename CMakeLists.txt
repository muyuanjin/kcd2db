cmake_minimum_required(VERSION 3.30)
project(kcd2db LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()


if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This project only supports Windows and MSVC")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
add_compile_options("/source-charset:utf-8")

include(FetchContent)

# Download and set up libmem
FetchContent_Declare(libmem-config URL "https://raw.githubusercontent.com/rdbo/libmem/config-v1/libmem-config.cmake" DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(libmem-config)
set(CMAKE_PREFIX_PATH "${libmem-config_SOURCE_DIR}" "${CMAKE_PREFIX_PATH}")
set(LIBMEM_DOWNLOAD_VERSION "5.0.4")

# Find libmem package
find_package(libmem CONFIG REQUIRED)

FetchContent_Declare(sqlite3 GIT_REPOSITORY https://github.com/sjinks/sqlite3-cmake GIT_TAG v3.49.1)
FetchContent_MakeAvailable(sqlite3)

# 定义SQLiteC++包装库
FetchContent_Declare(
        SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp
        GIT_TAG 3.3.1  # 使用正式release版本
)
# 自动配置SQLiteCpp（使用现代方法）
FetchContent_MakeAvailable(SQLiteCpp)

file(GLOB_RECURSE KCD2DB_SOURCES
        "src/*.cpp"
        "src/*.h"
)
add_library(kcd2db SHARED ${KCD2DB_SOURCES}
        src/db/database.cpp
        src/db/database.h)
# Define LM_EXPORT to use libmem as a static library
target_compile_definitions(kcd2db PRIVATE LM_EXPORT)

target_link_libraries(kcd2db PRIVATE libmem::libmem ntdll SQLiteCpp SQLite::SQLite3)

set_target_properties(kcd2db PROPERTIES SUFFIX ".asi" PREFIX "")
