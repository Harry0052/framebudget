cmake_minimum_required(VERSION 3.16)
project(framebudget VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc videoio)

add_library(framebudget_core
  src/sha256.cpp
  src/labels.cpp
  src/media.cpp
  src/scoring.cpp
  src/evaluate.cpp
  src/report.cpp
  src/cli.cpp
)
target_include_directories(framebudget_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(framebudget_core SYSTEM PUBLIC
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(framebudget_core PUBLIC ${OpenCV_LIBS} avutil)
target_compile_definitions(framebudget_core PUBLIC FRAMEBUDGET_VERSION="${PROJECT_VERSION}")
target_compile_options(framebudget_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

option(FRAMEBUDGET_SANITIZERS "Build with address and undefined-behavior sanitizers" OFF)
if(FRAMEBUDGET_SANITIZERS)
  target_compile_options(framebudget_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(framebudget_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(framebudget src/main.cpp)
target_link_libraries(framebudget PRIVATE framebudget_core)
target_compile_options(framebudget PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(framebudget_tests tests/unit.cpp)
target_link_libraries(framebudget_tests PRIVATE framebudget_core)
target_compile_definitions(framebudget_tests PRIVATE FRAMEBUDGET_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures")
target_compile_options(framebudget_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME unit COMMAND framebudget_tests)
