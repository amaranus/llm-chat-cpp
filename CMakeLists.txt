cmake_minimum_required(VERSION 3.16)
project(llm-chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(READLINE REQUIRED readline)
find_package(Threads REQUIRED)

add_executable(llm-chat src/main.cpp)
target_include_directories(llm-chat PRIVATE ${CURL_INCLUDE_DIRS} ${READLINE_INCLUDE_DIRS})
target_link_libraries(llm-chat PRIVATE ${CURL_LIBRARIES} ${READLINE_LIBRARIES} Threads::Threads)

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()
