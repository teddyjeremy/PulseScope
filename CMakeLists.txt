cmake_minimum_required(VERSION 3.20)
project(PulseScope VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PULSESCOPE_CORE
  src/core/timestamp.cpp
  src/core/json.cpp
  src/core/identity.cpp
  src/core/session.cpp
  src/core/schedule.cpp
  src/core/cpu.cpp
  src/core/memory.cpp
  src/core/compare.cpp
  src/core/filter.cpp
  src/core/timeline.cpp
  src/core/export.cpp
  src/core/bookmarks.cpp
  src/core/fixture.cpp
  src/core/validate.cpp
  src/core/settings.cpp
  src/core/log.cpp
)

add_library(pulsescope_core STATIC ${PULSESCOPE_CORE})
target_include_directories(pulsescope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pulsescope_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(pulsescope_core PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(pulsescope_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pulsescope-cli src/cli/main.cpp)
target_link_libraries(pulsescope-cli PRIVATE pulsescope_core)
if(NOT MSVC)
  target_link_libraries(pulsescope-cli PRIVATE pthread)
endif()

add_executable(pulsescope-tests tests/test_main.cpp tests/engine_tests.cpp)
target_include_directories(pulsescope-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(pulsescope-tests PRIVATE pulsescope_core)
if(NOT MSVC)
  target_link_libraries(pulsescope-tests PRIVATE pthread)
endif()

enable_testing()
add_test(NAME engine COMMAND pulsescope-tests)

if(WIN32)
  add_executable(PulseScope WIN32
    src/win/main.cpp
    src/win/etw_decode.cpp
    src/win/etw_session.cpp
    src/win/live_sampler.cpp
    src/win/ui_app.cpp
    src/win/pulsescope.rc
  )
  target_compile_definitions(PulseScope PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(PulseScope PRIVATE pulsescope_core d2d1 dwrite ole32 shell32 advapi32 tdh psapi)
  if(MSVC)
    target_compile_options(PulseScope PRIVATE /W4 /permissive- /EHsc)
    set_target_properties(PulseScope PROPERTIES
      LINK_FLAGS "/MANIFEST:NO"
    )
  endif()
endif()
