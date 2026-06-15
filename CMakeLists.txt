cmake_minimum_required(VERSION 3.16)
project(llm-chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(READLINE REQUIRED readline)
find_package(Threads REQUIRED)

set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/http_client.cpp
    src/mcp_client.cpp
    src/llm_client.cpp
    src/chat_app.cpp
)

add_executable(llm-chat ${SOURCES})
target_include_directories(llm-chat PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
    ${READLINE_INCLUDE_DIRS}
)
target_link_libraries(llm-chat PRIVATE
    ${CURL_LIBRARIES}
    ${READLINE_LIBRARIES}
    Threads::Threads
)

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()