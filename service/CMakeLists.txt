# bump version here
set(service_VERSION 1.7)

define_module(LIBRARY service=${service_VERSION}
  DEPENDS utility>=1.11 dbglog>=1.4
  Boost_FILESYSTEM Boost_PROGRAM_OPTIONS Boost_SYSTEM
  # we need pthread_* stuff
  THREADS)

compile_with_hostname()

set(service_SOURCES
  program.hpp program.cpp
  cmdline.hpp cmdline.cpp

  runninguntilsignalled.hpp runninguntilsignalled.cpp
  )

if(WIN32)
  # windows
else()
  # posix
  list(APPEND service_SOURCES
    service.hpp service.cpp
    pidfile.hpp pidfile.cpp
    detail/signalhandler.hpp detail/signalhandler.cpp
    ctrlclient.hpp ctrlclient.cpp
  )
endif()

add_customer_sources_with_default(service service_SOURCES
  customer/program.{CUSTOMER}.cpp)

add_library(service STATIC ${service_SOURCES})
buildsys_library(service)

target_link_libraries(service ${MODULE_LIBRARIES})
buildsys_target_compile_definitions(service ${MODULE_DEFINITIONS})

add_subdirectory(tools EXCLUDE_FROM_ALL)
