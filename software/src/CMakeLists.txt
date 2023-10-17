cmake_minimum_required (VERSION 3.1)

project (mifare C)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(SRC_DIR ./)

set(COMMON_FILES
    ${SRC_DIR}/common.c
    ${SRC_DIR}/crapto1.c
    ${SRC_DIR}/crypto1.c
    ${SRC_DIR}/bucketsort.c
    ${SRC_DIR}/parity.c)

set(
    NESTED_UTIL
    ${SRC_DIR}/nested_util.c
)

set(
    MFKEY_UTIL
    ${SRC_DIR}/mfkey.c
)

include_directories(
    ${SRC_DIR}/
    )

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    MESSAGE(STATUS "Run on linux.")

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    endif()
    
    add_compile_options(-D_GNU_SOURCE)

    find_package(Threads)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    MESSAGE(STATUS "Run on Windows.")

    #  Add pthread header for Windows
    include_directories(lib/pthread/include)

    # optimize
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Ox")
    endif()

    # pthread need
    add_definitions(-DHAVE_STRUCT_TIMESPEC)

    # find the pthread dep
    find_library(LIBTHREAD pthreadVC2.lib lib/pthread/lib/x64/)
else()
    MESSAGE(STATUS "other platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ignore warning C4996
add_compile_options(-D_CRT_SECURE_NO_WARNINGS)

# tools
add_executable(nested ${COMMON_FILES} ${NESTED_UTIL} nested.c)
target_link_libraries(nested ${LIBTHREAD})

add_executable(staticnested ${COMMON_FILES} ${NESTED_UTIL} staticnested.c)
target_link_libraries(staticnested ${LIBTHREAD})

add_executable(darkside ${COMMON_FILES} ${MFKEY_UTIL} darkside.c)

add_executable(mfkey32 ${COMMON_FILES} mfkey32.c)
add_executable(mfkey32v2 ${COMMON_FILES} mfkey32v2.c)
add_executable(mfkey64 ${COMMON_FILES} mfkey64.c)
