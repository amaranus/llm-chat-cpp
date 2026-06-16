cmake_minimum_required(VERSION 3.16)
project(llm-chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

if(WIN32)
    find_package(CURL REQUIRED)
    add_compile_options(/utf-8)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED libcurl)
    pkg_check_modules(READLINE REQUIRED readline)
endif()

set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/http_client.cpp
    src/mcp_client.cpp
    src/llm_client.cpp
    src/chat_app.cpp
)

if(WIN32)
    list(APPEND SOURCES src/readline_compat.cpp)
endif()

add_executable(llm-chat ${SOURCES})

target_include_directories(llm-chat PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(WIN32)
    target_include_directories(llm-chat PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(llm-chat PRIVATE ${CURL_LIBRARIES} Threads::Threads)
else()
    target_include_directories(llm-chat PRIVATE ${CURL_INCLUDE_DIRS} ${READLINE_INCLUDE_DIRS})
    target_link_libraries(llm-chat PRIVATE ${CURL_LIBRARIES} ${READLINE_LIBRARIES} Threads::Threads)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()