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

# --- liblzma Build ---
# NOTE: Ensure the path 'xz' matches the actual directory name containing liblzma source
set(LIBLZMA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/xz)
# Define the build directory *relative* to the liblzma source directory
set(LIBLZMA_BUILD_SUBDIR build)
set(LIBLZMA_BUILD_DIR ${LIBLZMA_SRC_DIR}/${LIBLZMA_BUILD_SUBDIR})

# Define CMake arguments for configuring liblzma
# Note: -B and -S are handled by the command structure below
set(LIBLZMA_CMAKE_ARGS
    -DXZ_TOOL_XZ=OFF
    -DXZ_TOOL_XZDEC=OFF
    -DXZ_TOOL_LZMADEC=OFF
    -DXZ_TOOL_LZMAINFO=OFF
    -DXZ_TOOL_SCRIPTS=OFF
    -DXZ_DOC=OFF
    -DXZ_DOXYGEN=OFF
    -DBUILD_SHARED_LIBS=OFF # Ensure static lib is built
    # Pass down the build type if using a multi-config generator (like VS)
    # or just to be consistent
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
)
# Add platform-specific args
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    list(APPEND LIBLZMA_CMAKE_ARGS "-DXZ_SANDBOX=no")
endif()

# Custom target to run the build commands for liblzma
# NOTE: This assumes the commands succeed. Error checking is minimal.
# NOTE: The build command below does NOT specify --config, so on VS it will likely default to Debug.
add_custom_target(build_liblzma ALL # Add ALL to ensure it runs with the default build
    # Command 1: Configure liblzma (using CMake)
    # Runs CMake from LIBLZMA_SRC_DIR, telling it to configure into the 'build' subdirectory
    COMMAND ${CMAKE_COMMAND} -B ${LIBLZMA_BUILD_SUBDIR} -S . ${LIBLZMA_CMAKE_ARGS}
    # Command 2: Build liblzma (using CMake)
    # Runs CMake's build tool mode, targeting the 'build' subdirectory
    # No --config flag here, relying on default behavior (likely Debug for VS)
    COMMAND ${CMAKE_COMMAND} --build ${LIBLZMA_BUILD_SUBDIR}
    # Specify the working directory where these commands should run
    WORKING_DIRECTORY ${LIBLZMA_SRC_DIR}
    COMMENT "Configuring and building liblzma via custom commands..."
    VERBATIM # Ensures commands are executed literally, important for special characters/spaces
)

# --- Define the expected path for the built liblzma library ---
# Hardcoding the path to the Debug directory on Windows

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    # Always point to the Debug directory and use lzma.lib
    set(LIBLZMA_LIB_PATH "${LIBLZMA_BUILD_DIR}/Debug/lzma.lib")
else()
    # Single-config (Linux Makefiles/Ninja): Library is typically directly in the build directory
    set(LIBLZMA_LIB_PATH "${LIBLZMA_BUILD_DIR}/liblzma.a")
endif()

# Create an IMPORTED library target for the custom-built liblzma
# The IMPORTED_LOCATION is set here, but the file doesn't need to exist at configure time.
# Added GLOBAL scope so the target is visible in other directory scopes if needed.
add_library(liblzma_imported STATIC IMPORTED GLOBAL)
set_target_properties(liblzma_imported PROPERTIES
    IMPORTED_LOCATION "${LIBLZMA_LIB_PATH}"
    # Associate the include directory with this target
    INTERFACE_INCLUDE_DIRECTORIES "${LIBLZMA_SRC_DIR}/src/liblzma/api"
)

# Add dependency: the imported target depends on the custom build finishing
add_dependencies(liblzma_imported build_liblzma)

# --- Rest of your CMakeLists.txt ---

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
    # Only include util_posix.c on non-Windows systems
)
if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
    list(APPEND HARDNESTED_SOURCES ${HARDNESTED_RECOVERY_DIR}/pm3/util_posix.c)
endif()


# Platform specific settings
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    MESSAGE(STATUS "Run on linux.")
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3") # Use specific variable
    endif()
    find_package(Threads REQUIRED)
    set(LIBTHREAD Threads::Threads) # Use modern target if available
    set(LIBMATH m)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    MESSAGE(STATUS "Run on Windows.")
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ox") # Use specific variable
    endif()
    # Ensure the path to pthread lib is correct relative to CMAKE_CURRENT_SOURCE_DIR
    find_library(PTHREAD_LIB pthreadVC2.lib PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib/pthread/lib/x64/)
    if (NOT PTHREAD_LIB)
        message(FATAL_ERROR "pthreadVC2.lib not found in ${CMAKE_CURRENT_SOURCE_DIR}/lib/pthread/lib/x64/")
    endif()
    # Create an imported library for pthread on Windows for consistency
    # Added GLOBAL scope
    add_library(pthread STATIC IMPORTED GLOBAL)
    set_target_properties(pthread PROPERTIES
        IMPORTED_LOCATION ${PTHREAD_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/lib/pthread/include
    )
    set(LIBTHREAD pthread) # Use the imported target name
    set(LIBMATH "")
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

# Consider creating a helper function/macro if platform-specific settings become complex or repetitive

add_executable(nested ${COMMON_FILES} ${NESTED_UTIL} nested.c)
target_include_directories(nested PRIVATE ${SRC_DIR})
target_link_libraries(nested PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(nested PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(nested PRIVATE HAVE_STRUCT_TIMESPEC)
    # Link pthread target to get include dirs automatically
    target_link_libraries(nested PRIVATE pthread)
endif()


add_executable(staticnested ${COMMON_FILES} ${NESTED_UTIL} staticnested.c)
target_include_directories(staticnested PRIVATE ${SRC_DIR})
target_link_libraries(staticnested PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(staticnested PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(staticnested PRIVATE HAVE_STRUCT_TIMESPEC)
    # Link pthread target to get include dirs automatically
    target_link_libraries(staticnested PRIVATE pthread)
endif()


add_executable(darkside ${COMMON_FILES} ${MFKEY_UTIL} darkside.c)
target_include_directories(darkside PRIVATE ${SRC_DIR})
# darkside doesn't seem to link pthreads, add if needed: target_link_libraries(darkside PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(darkside PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(darkside PRIVATE HAVE_STRUCT_TIMESPEC)
    # If it needs pthread headers, link the target:
    # target_link_libraries(darkside PRIVATE pthread)
endif()


add_executable(mfkey32 ${COMMON_FILES} mfkey32.c)
target_include_directories(mfkey32 PRIVATE ${SRC_DIR})
# mfkey32 doesn't seem to link pthreads, add if needed: target_link_libraries(mfkey32 PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(mfkey32 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey32 PRIVATE HAVE_STRUCT_TIMESPEC)
    # If it needs pthread headers, link the target:
    # target_link_libraries(mfkey32 PRIVATE pthread)
endif()


add_executable(mfkey32v2 ${COMMON_FILES} mfkey32v2.c)
target_include_directories(mfkey32v2 PRIVATE ${SRC_DIR})
# mfkey32v2 doesn't seem to link pthreads, add if needed: target_link_libraries(mfkey32v2 PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(mfkey32v2 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey32v2 PRIVATE HAVE_STRUCT_TIMESPEC)
    # If it needs pthread headers, link the target:
    # target_link_libraries(mfkey32v2 PRIVATE pthread)
endif()


add_executable(mfkey64 ${COMMON_FILES} mfkey64.c)
target_include_directories(mfkey64 PRIVATE ${SRC_DIR})
# mfkey64 doesn't seem to link pthreads, add if needed: target_link_libraries(mfkey64 PRIVATE ${LIBTHREAD})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(mfkey64 PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(mfkey64 PRIVATE HAVE_STRUCT_TIMESPEC)
    # If it needs pthread headers, link the target:
    # target_link_libraries(mfkey64 PRIVATE pthread)
endif()


add_executable(hardnested ${COMMON_FILES} ${HARDNESTED_SOURCES})
# Add dependency: hardnested depends on the *imported library target*
# CMake handles the transitive dependency: hardnested -> liblzma_imported -> build_liblzma
add_dependencies(hardnested liblzma_imported)

target_include_directories(hardnested PRIVATE
    ${SRC_DIR}
    ${HARDNESTED_RECOVERY_DIR}
    ${HARDNESTED_RECOVERY_DIR}/pm3
    ${HARDNESTED_RECOVERY_DIR}/hardnested
    # Include dir now comes from linking liblzma_imported via INTERFACE_INCLUDE_DIRECTORIES
)
# Note: -fPIC is generally not needed for executables, only shared libraries or static libs used by shared libs.
# Removed -fPIC from the context provided earlier unless specifically required.
target_compile_options(hardnested PRIVATE -Wall) # Consider adding more warnings like -Wextra -pedantic
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(hardnested PRIVATE _GNU_SOURCE)
endif()
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    # Add LZMA_API_STATIC for Windows builds linking statically
    target_compile_definitions(hardnested PRIVATE
        HAVE_STRUCT_TIMESPEC
        LZMA_API_STATIC  # <--- Added this line
    )
    # Link pthread target to get include dirs automatically
    target_link_libraries(hardnested PRIVATE pthread)
endif()

target_link_libraries(hardnested PRIVATE
    ${LIBTHREAD}
    ${LIBMATH} # Link math library if needed (usually only Linux/non-Windows)
    liblzma_imported # Link against the IMPORTED target name
)
