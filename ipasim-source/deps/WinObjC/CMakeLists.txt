cmake_minimum_required (VERSION 3.11)

project (winobjc)

set (CL_COMPILER ON)

list (APPEND CMAKE_MODULE_PATH "${SOURCE_DIR}/scripts")
include (CommonVariables)

add_prep_target (config-winobjc-x86-Debug) # TODO: Don't hardcode this.

add_subdirectory (tools)
add_subdirectory (Frameworks)
