cmake_minimum_required(VERSION 3.25)
project(covirt VERSION 0.1.0 LANGUAGES C CXX)

SET(CMAKE_CXX_STANDARD 23)

configure_file(version.h.in version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/covirt)

include(FetchContent)

find_package(Zydis QUIET)
if (NOT Zydis_FOUND)
    FetchContent_Declare(
        Zydis
        GIT_REPOSITORY https://github.com/zyantific/zydis.git
        GIT_TAG v4.1.0
    )

    FetchContent_MakeAvailable(Zydis)
endif()

find_package(LIEF QUIET)
if (NOT LIEF_FOUND)
    FetchContent_Declare(
        LIEF
        GIT_REPOSITORY https://github.com/lief-project/LIEF
        GIT_TAG v0.15.1
    )

    FetchContent_MakeAvailable(LIEF)
endif()

find_package(zasm QUIET)
if (NOT zasm_FOUND)
    FetchContent_Declare(
        zasm
        GIT_REPOSITORY https://github.com/zyantific/zasm
        GIT_BRANCH master
    )

    FetchContent_MakeAvailable(zasm)
endif()

file(GLOB_RECURSE COVIRT_GLOB_SRC CONFIGURE_DEPENDS "covirt/*.cpp")

add_executable(covirt ${COVIRT_GLOB_SRC})
target_link_libraries(covirt LIEF Zydis zasm)