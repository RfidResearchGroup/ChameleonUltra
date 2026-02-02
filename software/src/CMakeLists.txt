cmake_minimum_required (VERSION 3.5)

project (mifare C)

include(FetchContent)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../script/bin)
set(SRC_DIR ./) # Assuming source files are in the same directory as CMakeLists.txt

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

if(CMAKE_CONFIGURATION_TYPES)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${EXECUTABLE_OUTPUT_PATH})
    endforeach()
endif()

# Define a variable for the compatibility code directory
set(COMPAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compat)

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

FetchContent_Declare(
    xz
    GIT_REPOSITORY "https://github.com/tukaani-project/xz"
    GIT_TAG "v5.8.1"
    OVERRIDE_FIND_PACKAGE
    EXCLUDE_FROM_ALL
)

set(XZ_TOOL_XZ OFF CACHE BOOL "")
set(XZ_TOOL_XZDEC OFF CACHE BOOL "")
set(XZ_TOOL_LZMADEC OFF CACHE BOOL "")
set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "")
set(XZ_TOOL_SCRIPTS OFF CACHE BOOL "")
set(XZ_DOC OFF CACHE BOOL "")
set(XZ_NLS OFF CACHE BOOL "")
set(XZ_DOXYGEN OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

FetchContent_MakeAvailable(xz)


# --- Hardnested Recovery Sources ---
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
)
if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
    list(APPEND HARDNESTED_SOURCES ${HARDNESTED_RECOVERY_DIR}/pm3/util_posix.c)
endif()


# --- Platform specific settings ---
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    MESSAGE(STATUS "Run on linux.")
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    endif()
    find_package(Threads REQUIRED)
    set(LIBTHREAD Threads::Threads) # Use modern target
    set(LIBMATH m)

elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    MESSAGE(STATUS "Run on Windows.")
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        # Set optimization flags based on compiler
        if(MSVC)
            set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ox")
        else() # Assuming MinGW or similar GCC-compatible
            set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
        endif()
    endif()

    FetchContent_Declare(
        pthreads4w
        GIT_REPOSITORY "https://github.com/GerHobbelt/pthread-win32"
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
    )
    find_package(pthreads4w CONFIG REQUIRED)
    set(LIBTHREAD pthreads4w::pthreadVC3)

    set(LIBMATH "") # No separate math library needed on Windows
else()
    # Handle other platforms or provide a default/error
    MESSAGE(STATUS "Running on other platform: ${CMAKE_SYSTEM_NAME}")
    set(LIBMATH "")
    # Attempt to find Threads anyway, might fail gracefully or error depending on REQUIRED
    find_package(Threads)
    if(Threads_FOUND)
      set(LIBTHREAD Threads::Threads)
    else()
      message(WARNING "Threads library not found for platform ${CMAKE_SYSTEM_NAME}. Linking might fail.")
      set(LIBTHREAD "") # Set to empty or handle error
    endif()
endif()

# --- Executable Definitions ---

add_executable(nested ${COMMON_FILES} ${NESTED_UTIL} nested.c)
target_include_directories(nested PRIVATE ${SRC_DIR})
target_link_libraries(nested PRIVATE ${LIBTHREAD}) # Link common thread lib
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(nested PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(nested PRIVATE HAVE_STRUCT_TIMESPEC)
    # No extra target_link_libraries needed here, ${LIBTHREAD} handles it
endif()


add_executable(staticnested ${COMMON_FILES} ${NESTED_UTIL} staticnested.c)
target_include_directories(staticnested PRIVATE ${SRC_DIR})
target_link_libraries(staticnested PRIVATE ${LIBTHREAD}) # Link common thread lib
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(staticnested PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(staticnested PRIVATE HAVE_STRUCT_TIMESPEC)
    # No extra target_link_libraries needed here, ${LIBTHREAD} handles it
endif()


add_executable(darkside ${COMMON_FILES} ${MFKEY_UTIL} darkside.c)
target_include_directories(darkside PRIVATE ${SRC_DIR})
# darkside doesn't seem to need pthreads based on original file
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(darkside PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(darkside PRIVATE HAVE_STRUCT_TIMESPEC)
endif()


add_executable(mfkey32 ${COMMON_FILES} mfkey32.c)
target_include_directories(mfkey32 PRIVATE ${SRC_DIR})
# mfkey32 doesn't seem to need pthreads based on original file
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(mfkey32 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey32 PRIVATE HAVE_STRUCT_TIMESPEC)
endif()


add_executable(mfkey32v2 ${COMMON_FILES} mfkey32v2.c)
target_include_directories(mfkey32v2 PRIVATE ${SRC_DIR})
# mfkey32v2 doesn't seem to need pthreads based on original file
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(mfkey32v2 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey32v2 PRIVATE HAVE_STRUCT_TIMESPEC)
endif()


add_executable(mfkey64 ${COMMON_FILES} mfkey64.c)
target_include_directories(mfkey64 PRIVATE ${SRC_DIR})
# mfkey64 doesn't seem to need pthreads based on original file
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(mfkey64 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey64 PRIVATE HAVE_STRUCT_TIMESPEC)
endif()

add_executable(staticnested_1nt ${COMMON_FILES} staticnested_1nt.c)
target_include_directories(staticnested_1nt PRIVATE ${SRC_DIR})
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(staticnested_1nt PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(staticnested_1nt PRIVATE HAVE_STRUCT_TIMESPEC)
endif()

add_executable(staticnested_2x1nt_rf08s ${COMMON_FILES} staticnested_2x1nt_rf08s.c)
target_include_directories(staticnested_2x1nt_rf08s PRIVATE ${SRC_DIR})
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(staticnested_2x1nt_rf08s PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(staticnested_2x1nt_rf08s PRIVATE HAVE_STRUCT_TIMESPEC)
endif()

add_executable(staticnested_2x1nt_rf08s_1key ${COMMON_FILES} staticnested_2x1nt_rf08s_1key.c)
target_include_directories(staticnested_2x1nt_rf08s_1key PRIVATE ${SRC_DIR})
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(staticnested_2x1nt_rf08s_1key PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(staticnested_2x1nt_rf08s_1key PRIVATE HAVE_STRUCT_TIMESPEC)
endif()

# --- mfulc_des_brute Executable ---
add_executable(mfulc_des_brute mfulc_des_brute.c)
target_include_directories(mfulc_des_brute PRIVATE ${SRC_DIR})
target_link_libraries(mfulc_des_brute PRIVATE ${LIBTHREAD} OpenSSL::Crypto)
if (MSVC)
    target_compile_options(mfulc_des_brute PRIVATE /Wd4996)  # disable "deprecated declaration" warning
else()
    target_compile_options(mfulc_des_brute PRIVATE -Wno-deprecated-declarations)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(mfulc_des_brute PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfulc_des_brute PRIVATE HAVE_STRUCT_TIMESPEC)
endif()
find_package(OpenSSL REQUIRED)

# --- hardnested Executable ---
add_executable(hardnested ${COMMON_FILES} ${HARDNESTED_SOURCES})

target_include_directories(hardnested PRIVATE
    ${SRC_DIR}
    ${HARDNESTED_RECOVERY_DIR}
    ${HARDNESTED_RECOVERY_DIR}/pm3
    ${HARDNESTED_RECOVERY_DIR}/hardnested
    ${xz_SOURCE_DIR}/src/liblzma/api
)
target_compile_options(hardnested PRIVATE -Wall)

if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(hardnested PRIVATE _GNU_SOURCE)
endif()

# Platform-specific settings for Windows
if (CMAKE_SYSTEM_NAME MATCHES "Windows")

    # Settings common to all Windows builds (MSVC & MinGW)
    target_compile_definitions(hardnested PRIVATE
        HAVE_STRUCT_TIMESPEC
        LZMA_API_STATIC # Keep if needed for static linking of lzma
    )
    # No extra target_link_libraries needed here, ${LIBTHREAD} handles it below

    # Add fmemopen compatibility layer ONLY for non-MSVC Windows builds (e.g., MinGW)
    if(NOT MSVC)
        message(STATUS "Non-MSVC Windows build detected, adding fmemopen compatibility layer.")
        target_sources(hardnested PRIVATE
            ${COMPAT_DIR}/fmemopen/libfmemopen.c # Compile the source file
        )
        target_include_directories(hardnested PRIVATE
             ${COMPAT_DIR}/fmemopen # Add include directory for fmemopen.h
        )
    endif() # End NOT MSVC

endif() # End Windows

# Link libraries common to all platforms (or handled by variables)
target_link_libraries(hardnested PRIVATE
    ${LIBTHREAD}    # Handles pthread correctly now for Linux, MSVC, MinGW
    ${LIBMATH}      # Handles 'm' on Linux, empty on Windows
    liblzma
)
