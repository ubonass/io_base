# Windows
############################################################################################################################################################################
# cmake -G  "Visual Studio 17 2022" -T ClangCL -DCMAKE_BUILD_TYPE:STRING="Debug" -DBASE_FETCH_ABSL=ON ../
############################################################################################################################################################################
# cmake -G  "Visual Studio 17 2022" -T ClangCL -DCMAKE_BUILD_TYPE:STRING="Release" -DBASE_FETCH_ABSL=ON ../
############################################################################################################################################################################

#Linux
############################################################################################################################################################################
# cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE:STRING="Debug" -DBASE_FETCH_ABSL=ON  ../
############################################################################################################################################################################
# cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE:STRING="Release" -DBASE_FETCH_ABSL=ON ../
############################################################################################################################################################################

# git clone https://quiche.googlesource.com/googleurl

cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there. Thank you.)
endif()

#
# Project configuration
#
set(BASE_VERSION_MAJOR 1)
set(BASE_VERSION_MINOR 0)
set(BASE_VERSION_PATCH 0)

project(
  IO_BASE
  VERSION ${BASE_VERSION_MAJOR}.${BASE_VERSION_MINOR}.${BASE_VERSION_PATCH}
  DESCRIPTION IO_BASE
  HOMEPAGE_URL https://github.com/ubonass/io_base.git
  LANGUAGES C CXX
)

# get rid of the extra default configurations
# what? why would you get id of other useful build types? - Ellzey
# set(CMAKE_CONFIGURATION_TYPES Debug;Release CACHE STRING Limited configurations FORCE)
# set(CMAKE_C_COMPILER "$ENV{LLVM_ROOT}/bin/clang-cl.exe")
# set(CMAKE_CXX_COMPILER "$ENV{LLVM_ROOT}/bin/clang-cl.exe")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH})

# must using CXX20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_definitions(UNICODE)
  add_compile_definitions(_UNICODE)
  add_compile_definitions(NOMINMAX)
  # add_compile_definitions(NOGDI)
  add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif()

option(BASE_FETCH_ABSL "Base FETCH ABSL " OFF)

#option(BASE_BUILD_TEST "Build test programs" OFF)

if(BASE_FETCH_ABSL)
  include(abseil-cpp)
endif()

list(APPEND BASE_SRCS
  logging.cc
  yield.cc
  yield_policy.cc
  task_queue_base.cc
  platform_thread_types.cc
  recursive_critical_section.cc
  event.cc
  event_loop.cc
)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  list(APPEND BASE_SRCS
    stacktrace.cc
  )
endif()

add_library(io_base
  ${BASE_SRCS}
)

target_link_libraries(io_base PRIVATE
  $<$<TARGET_EXISTS:absl::base>:absl::base>
  $<$<TARGET_EXISTS:absl::algorithm>:absl::algorithm>
  $<$<TARGET_EXISTS:absl::flags>:absl::flags>
  $<$<TARGET_EXISTS:absl::absl_check>:absl::absl_check>
  $<$<TARGET_EXISTS:absl::absl_log>:absl::absl_log>
  $<$<TARGET_EXISTS:absl::strings>:absl::strings>
  $<$<TARGET_EXISTS:absl::str_format>:absl::str_format>
  $<$<TARGET_EXISTS:absl::synchronization>:absl::synchronization>
  $<$<TARGET_EXISTS:absl::base>:absl::time>
  $<$<TARGET_EXISTS:absl::base>:absl::optional>
  $<$<TARGET_EXISTS:absl::utility>:absl::utility>
)

target_include_directories(io_base PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(io_base::io_base ALIAS io_base)

