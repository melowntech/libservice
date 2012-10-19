define_module(LIBRARY service
  DEPENDS dbglog Boost_PROGRAM_OPTIONS Boost_SYSTEM)

set(service_SOURCES
    program.cpp service.cpp cmdline.cpp
)

add_library(service STATIC ${service_SOURCES})
set_target_properties(service PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
  ${CMAKE_SOURCE_DIR}/lib)

target_link_libraries(service ${MODULE_LIBRARIES})
