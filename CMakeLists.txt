cmake_minimum_required(VERSION 3.21)
project(cell LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ═══════════════════════════════════════════════════════════════════════
#  依赖下载 (FetchContent)
# ═══════════════════════════════════════════════════════════════════════

# ── nlohmann/json (header-only，直接可用) ─────────────────────────────
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(json)

# ── libsodium (autotools，需要手动构建) ──────────────────────────────
FetchContent_Declare(sodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG        1.0.20
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_GetProperties(sodium)
if(NOT sodium_POPULATED)
    FetchContent_Populate(sodium)
endif()

set(SODIUM_PREFIX "${CMAKE_BINARY_DIR}/deps/sodium")

find_program(MAKE_PROG make gmake)
if(NOT MAKE_PROG)
    set(MAKE_PROG "make")
endif()
include(ProcessorCount)
ProcessorCount(NCPU)
if(NCPU EQUAL 0)
    set(NCPU 4)
endif()

if(WIN32)
    # Windows: libsodium 的 MSVC 构建
    set(SODIUM_LIB_OUT "${SODIUM_PREFIX}/lib/sodium.lib")
    add_custom_command(
        OUTPUT  "${SODIUM_LIB_OUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SODIUM_PREFIX}/lib"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SODIUM_PREFIX}/include"
        COMMAND cmd /c "cd /d \"${sodium_SOURCE_DIR}/builds/msvc\" && build.bat static"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${sodium_SOURCE_DIR}/builds/msvc/output/x64/Release/libsodium.lib"
            "${SODIUM_LIB_OUT}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${sodium_SOURCE_DIR}/src/libsodium/include"
            "${SODIUM_PREFIX}/include"
        WORKING_DIRECTORY "${sodium_SOURCE_DIR}"
        COMMENT "Building libsodium (MSVC static)"
        VERBATIM
    )
else()
    # Unix: autotools
    set(SODIUM_LIB_OUT "${SODIUM_PREFIX}/lib/libsodium.a")
    add_custom_command(
        OUTPUT  "${SODIUM_LIB_OUT}"
        COMMAND "${sodium_SOURCE_DIR}/configure"
            "--prefix=${SODIUM_PREFIX}"
            --enable-static --disable-shared
        COMMAND ${MAKE_PROG} -j${NCPU}
        COMMAND ${MAKE_PROG} install
        WORKING_DIRECTORY "${sodium_SOURCE_DIR}"
        COMMENT "Building libsodium (autotools static)"
    )
endif()

add_custom_target(sodium_build DEPENDS "${SODIUM_LIB_OUT}")

add_library(sodium STATIC IMPORTED GLOBAL)
set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION             "${SODIUM_LIB_OUT}"
    INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_PREFIX}/include"
)
add_dependencies(sodium sodium_build)

# ── curl (native CMake，直接 FetchContent) ───────────────────────────
set(CURL_EXTRA_ARGS "")
if(APPLE)
    execute_process(
        COMMAND brew --prefix openssl
        OUTPUT_VARIABLE _ssl_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if(NOT _ssl_prefix)
        set(_ssl_prefix "/opt/homebrew/opt/openssl")
    endif()
    list(APPEND CURL_EXTRA_ARGS
        -DCMAKE_PREFIX_PATH=${_ssl_prefix}
        -DOPENSSL_ROOT_DIR=${_ssl_prefix}
    )
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE    OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
set(HTTP_ONLY         ON  CACHE BOOL "" FORCE)
set(ENABLE_UNIX_SOCKETS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP   ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS  ON  CACHE BOOL "" FORCE)

FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_12_1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    CMAKE_ARGS     ${CURL_EXTRA_ARGS}
)
FetchContent_MakeAvailable(curl)

# ═══════════════════════════════════════════════════════════════════════
#  可执行文件
# ═══════════════════════════════════════════════════════════════════════

add_executable(cell cell.cpp)

# ── 链接 ─────────────────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(cell PRIVATE
        nlohmann_json::nlohmann_json
        CURL::libcurl
        sodium
        "-framework Security"
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
        z pthread
    )
    target_link_options(cell PRIVATE -static-libgcc -static-libstdc++)

elseif(UNIX)
    target_link_libraries(cell PRIVATE
        nlohmann_json::nlohmann_json
        CURL::libcurl
        sodium
        ssl crypto z pthread dl
    )
    target_link_options(cell PRIVATE
        -static-libgcc -static-libstdc++
    )

elseif(WIN32)
    target_link_libraries(cell PRIVATE
        nlohmann_json::nlohmann_json
        CURL::libcurl
        sodium
        ws2_32 crypt32 wldap32 advapi32
    )
    target_compile_options(cell PRIVATE /MT)
    target_link_options(cell PRIVATE /NODEFAULTLIB:MSVCRT /NODEFAULTLIB:MSVCRTD)
endif()
