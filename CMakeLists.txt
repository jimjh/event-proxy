cmake_minimum_required (VERSION 3.8)
project (event-proxy)

# -- VARS --
set(LISTEN_BACKLOG 10)

# -- HEADERS --

# configure a header file to pass some of the CMake settings
# to the source code
configure_file (
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}")
include_directories (lib)  # specifies where to look for included header files
include_directories ("/projects/zlog/src")
include_directories ("/usr/local/opt/libevent/include")

# -- LIBRARIES --
find_library(ZLOG_LIB zlog HINTS "${PROJECT_SOURCE_DIR}/../zlog/src")

# -- SOURCES --

# add target for compilation db (so that we can use YouCompleteMe)
set(COMPILATION_DB "${PROJECT_SOURCE_DIR}/compile_commands.json")
add_custom_command (OUTPUT "${COMPILATION_DB}"
  COMMAND "${CMAKE_COMMAND}" -E create_symlink "${PROJECT_BINARY_DIR}/compile_commands.json" "${PROJECT_SOURCE_DIR}/compile_commands.json"
  COMMENT "symlinking compilation database"
)
add_custom_target (link_compile_commands_json ALL DEPENDS "${COMPILATION_DB}")

# let executable depend on that one
file(GLOB sources "src/*.c" "src/*.h")
add_executable (main ${sources})
target_link_libraries(main "${ZLOG_LIB}")

# -- GCC --

# gcc args
add_definitions(-std=c11)
add_definitions(-Werror)
add_definitions(-Wall)
add_definitions(-Wextra)

# generate compilation database for parsers
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
