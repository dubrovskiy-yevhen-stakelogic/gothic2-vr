cmake_minimum_required(VERSION 3.16)
project(TempestGpuTimingTests LANGUAGES CXX)

enable_testing()
add_executable(gpu-timing-tests main.cpp)
target_compile_features(gpu-timing-tests PRIVATE cxx_std_20)
target_include_directories(gpu-timing-tests PRIVATE ../../Engine/include)

# Reuse a built shared Tempest library when testing a consuming application's build.
# Otherwise this is a standalone test project with no game assets or shader compiler.
set(TEMPEST_TEST_LIBRARY "" CACHE FILEPATH "Optional prebuilt shared Tempest import library")
if(TEMPEST_TEST_LIBRARY)
  target_link_libraries(gpu-timing-tests PRIVATE "${TEMPEST_TEST_LIBRARY}")
else()
  add_subdirectory(../../Engine tempest)
  target_link_libraries(gpu-timing-tests PRIVATE Tempest)
endif()
add_test(NAME gpu-timestamp-math COMMAND gpu-timing-tests --math-only)
add_test(NAME gpu-timestamp-vulkan COMMAND gpu-timing-tests)
set_tests_properties(gpu-timestamp-vulkan PROPERTIES SKIP_RETURN_CODE 77)
