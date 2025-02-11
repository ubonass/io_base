include(FetchContent)
set(ABSL_ENABLE_INSTALL ON) # now you can enable install rules even in subproject... 
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG        20240722.0
  #SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp 
)
if(NOT absl_POPULATED)
  FetchContent_Populate(absl)
  add_subdirectory(${absl_SOURCE_DIR} ${absl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# absl::algorithm
# absl::base
# absl::debugging
# absl::flat_hash_map
# absl::flags
# absl::memory
# absl::meta
# absl::numeric
# absl::random_random
# absl::strings
# absl::synchronization
# absl::time
# absl::utility


