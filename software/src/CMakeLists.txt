cmake_minimum_required (VERSION 3.1)

project (mifare C)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(SRC_DIR ./)

set(COMMON_FILES
    ${SRC_DIR}/common.c
    ${SRC_DIR}/crapto1.c
    ${SRC_DIR}/crypto1.c
    ${SRC_DIR}/bucketsort.c
    ${SRC_DIR}/mfkey.c
    ${SRC_DIR}/nested_util.c
    ${SRC_DIR}/parity.c)

include_directories(
    ${SRC_DIR}/
    )


# tools
add_executable(nested ${COMMON_FILES} nested.c)
add_executable(staticnested ${COMMON_FILES} staticnested.c)
add_executable(darkside ${COMMON_FILES} darkside.c)
add_executable(mfkey32 ${COMMON_FILES} mfkey32.c)
add_executable(mfkey32v2 ${COMMON_FILES} mfkey32v2.c)
add_executable(mfkey64 ${COMMON_FILES} mfkey64.c)
