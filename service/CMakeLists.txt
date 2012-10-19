define_module(LIBRARY service
  DEPENDS dbglog Boost_PROGRAM_OPTIONS)

set(service_SOURCES
    program.cpp service.cpp cmdline.cpp
)

add_library(service STATIC ${service_SOURCES})
target_link_libraries(service ${MODULE_SERVICE_LIBRARIES_OWN})
