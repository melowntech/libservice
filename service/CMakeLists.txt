define_module(LIBRARY service
  DEPENDS dbglog Boost_PROGRAM_OPTIONS Boost_SYSTEM)

compile_with_hostname()

set(service_SOURCES
    program.hpp program.cpp
    service.hpp service.cpp
    cmdline.hpp cmdline.cpp
    runable.hpp
)

add_library(service STATIC ${service_SOURCES})
buildsys_library(service)

target_link_libraries(service ${MODULE_LIBRARIES})
