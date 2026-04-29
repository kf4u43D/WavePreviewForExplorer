add_executable(com-thumbnail-smoke-cli main.cpp)

target_include_directories(com-thumbnail-smoke-cli PRIVATE
  ${PROJECT_SOURCE_DIR}/src/Common
)

target_link_libraries(com-thumbnail-smoke-cli PRIVATE ole32 gdi32)
target_compile_definitions(com-thumbnail-smoke-cli PRIVATE UNICODE _UNICODE NOMINMAX)
