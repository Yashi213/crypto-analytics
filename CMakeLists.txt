cmake_minimum_required(VERSION 3.16)
project(crypto_analytics VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json для clangd / VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)

# ─── Зависимости (устанавливаются через vcpkg, см. build.bat) ─────────────────
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

# libharu во vcpkg не отдаёт стандартный CMake-конфиг — ищем библиотеку напрямую
find_library(HPDF_LIB NAMES hpdf libhpdf)
if(NOT HPDF_LIB)
    message(FATAL_ERROR "libharu (hpdf) не найдена. Установка: vcpkg install libharu")
endif()

# ─── Цель ─────────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(crypto_analytics ${SOURCES})

target_include_directories(crypto_analytics PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(crypto_analytics PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    libpqxx::pqxx
    ${HPDF_LIB}
)

if(WIN32)
    target_link_libraries(crypto_analytics PRIVATE ws2_32 wldap32 crypt32 secur32)
endif()

if(MSVC)
    target_compile_options(crypto_analytics PRIVATE /W3 /utf-8)
else()
    target_compile_options(crypto_analytics PRIVATE -Wall -Wextra)
endif()

# ─── Положить config и sql рядом с бинарником ─────────────────────────────────
add_custom_command(TARGET crypto_analytics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:crypto_analytics>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/sql $<TARGET_FILE_DIR:crypto_analytics>/sql
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:crypto_analytics>/reports
)
