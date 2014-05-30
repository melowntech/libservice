define_module(LIBRARY service
  DEPENDS utility dbglog Boost_FILESYSTEM Boost_PROGRAM_OPTIONS Boost_SYSTEM
  # we need pthread_* stuff
  THREADS)

compile_with_hostname()

set(service_SOURCES
    program.hpp program.cpp
    service.hpp service.cpp
    detail/signalhandler.hpp detail/signalhandler.cpp
    cmdline.hpp cmdline.cpp
    pidfile.hpp pidfile.cpp
)

add_library(service STATIC ${service_SOURCES})
buildsys_library(service)

target_link_libraries(service ${MODULE_LIBRARIES})
buildsys_target_compile_definitions(service ${MODULE_DEFINITIONS})
