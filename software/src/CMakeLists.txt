cmake_minimum_required (VERSION 3.5)

project (mifare C)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../script/bin)
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


set(HARDNESTED_RECOVERY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/HardnestedRecovery)

set(HARDNESTED_SOURCES
    ${HARDNESTED_RECOVERY_DIR}/hardnested_main.c
    ${HARDNESTED_RECOVERY_DIR}/pm3/ui.c
    ${HARDNESTED_RECOVERY_DIR}/pm3/util.c
    ${HARDNESTED_RECOVERY_DIR}/cmdhfmfhard.c
    ${HARDNESTED_RECOVERY_DIR}/pm3/commonutil.c
    ${HARDNESTED_RECOVERY_DIR}/hardnested/hardnested_bf_core.c
    ${HARDNESTED_RECOVERY_DIR}/hardnested/hardnested_bruteforce.c
    ${HARDNESTED_RECOVERY_DIR}/hardnested/hardnested_bitarray_core.c
    ${HARDNESTED_RECOVERY_DIR}/hardnested/tables.c
    ${HARDNESTED_RECOVERY_DIR}/pm3/util_posix.c
)


include_directories(
    ${SRC_DIR}/
    ${HARDNESTED_RECOVERY_DIR}
    ${HARDNESTED_RECOVERY_DIR}/pm3
    ${HARDNESTED_RECOVERY_DIR}/hardnested
    )

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    MESSAGE(STATUS "Run on linux.")

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    endif()

    add_compile_options(-D_GNU_SOURCE)

    find_package(Threads REQUIRED)
    set(LIBTHREAD pthread)
    set(LIBMATH m)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    MESSAGE(STATUS "Run on Windows.")

    include_directories(lib/pthread/include)


    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Ox")
    endif()
    add_definitions(-DHAVE_STRUCT_TIMESPEC)
    find_library(LIBTHREAD pthreadVC2.lib lib/pthread/lib/x64/)
    set(LIBMATH "")
else()
    MESSAGE(STATUS "other platform: ${CMAKE_SYSTEM_NAME}")
    set(LIBMATH "")
endif()

add_executable(nested ${COMMON_FILES} ${NESTED_UTIL} nested.c)
target_link_libraries(nested PRIVATE ${LIBTHREAD})

add_executable(staticnested ${COMMON_FILES} ${NESTED_UTIL} staticnested.c)
target_link_libraries(staticnested PRIVATE ${LIBTHREAD})

add_executable(darkside ${COMMON_FILES} ${MFKEY_UTIL} darkside.c)

add_executable(mfkey32 ${COMMON_FILES} mfkey32.c)
add_executable(mfkey32v2 ${COMMON_FILES} mfkey32v2.c)
add_executable(mfkey64 ${COMMON_FILES} mfkey64.c)


add_executable(hardnested ${COMMON_FILES} ${HARDNESTED_SOURCES})

target_compile_options(hardnested PRIVATE -Wall -fPIC )

target_link_libraries(hardnested PRIVATE
    ${LIBTHREAD}
    ${LIBMATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/xz-5.8.1/build/liblzma.a
)
