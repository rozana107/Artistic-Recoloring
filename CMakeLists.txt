cmake_minimum_required(VERSION 3.16)
project(ArtisticRecoloring LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs ximgproc)

set(IMAGE_ABSTRACTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ImageAbstraction" CACHE PATH
    "Source directory of the supplied ImageAbstraction implementation")
if(NOT EXISTS "${IMAGE_ABSTRACTION_DIR}/include/image_abstraction.h")
    message(FATAL_ERROR "Set IMAGE_ABSTRACTION_DIR to the supplied ImageAbstraction folder.")
endif()
# Compile the supplied source unchanged with this project's compiler/OpenCV.
# Do not link its MSVC library into the portable GCC executable.
add_library(paper_abstraction
    "${IMAGE_ABSTRACTION_DIR}/src/image_abstraction.cpp"
    "${IMAGE_ABSTRACTION_DIR}/src/crgf.cpp"
    "${IMAGE_ABSTRACTION_DIR}/src/slic_background.cpp")
target_include_directories(paper_abstraction PUBLIC "${IMAGE_ABSTRACTION_DIR}/include" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(paper_abstraction PUBLIC opencv_core opencv_imgproc)

add_library(label_io label_io.cpp)
target_include_directories(label_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(label_io PUBLIC opencv_core opencv_imgcodecs)

add_library(recolor_core recolor.cpp)
target_include_directories(recolor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(recolor_core PUBLIC opencv_core opencv_imgproc)
if(MSVC)
    target_compile_options(recolor_core PRIVATE /W4)
else()
    target_compile_options(recolor_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(recolor main.cpp)
set_target_properties(recolor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
target_link_libraries(recolor PRIVATE recolor_core label_io paper_abstraction opencv_imgcodecs opencv_ximgproc)

include(CTest)
if(BUILD_TESTING)
    add_executable(recolor_tests tests.cpp)
    target_link_libraries(recolor_tests PRIVATE recolor_core)
    add_test(NAME recoloring_correctness COMMAND recolor_tests)
    add_executable(abstraction_integration_tests abstraction_integration_tests.cpp)
    target_link_libraries(abstraction_integration_tests PRIVATE recolor_core label_io paper_abstraction)
    add_test(NAME abstraction_integration COMMAND abstraction_integration_tests)
    add_test(NAME cli_help COMMAND recolor --help)
    add_test(NAME cli_invalid_mask COMMAND recolor in.png out.png --mask 0)
    set_tests_properties(cli_invalid_mask PROPERTIES WILL_FAIL TRUE)
endif()

# Portable GCC builds can keep the compiler runtime inside the executables.
if(MINGW)
    target_link_options(recolor PRIVATE -static)
    if(BUILD_TESTING)
        target_link_options(recolor_tests PRIVATE -static)
        target_link_options(abstraction_integration_tests PRIVATE -static)
    endif()
endif()
