add_executable(${PROJECT_NAME}example
        "${CMAKE_CURRENT_LIST_DIR}/Example.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/SamplePackFileImpl.h"
        "${CMAKE_CURRENT_LIST_DIR}/SamplePackFileImpl.cpp")

target_link_libraries(${PROJECT_NAME}example PUBLIC lib${PROJECT_NAME})

target_include_directories(
        ${PROJECT_NAME}example PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
