# bump version here
set(service_VERSION 1.11)

set(service_EXTRA_DEPENDS)

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
    detail/ctrlclient.hpp detail/ctrlclient.cpp
    netctrlclient.hpp netctrlclient.cpp
    ctrlhandshake.hpp ctrlhandshake.cpp
    )
  if (NOT APPLE)
    list(APPEND service_SOURCES
      pipenotifier.hpp pipenotifier.cpp
    )
  endif()
endif()

### vvv add build date and time vvv
string(TIMESTAMP service_build_timestamp "%Y-%m-%d %H:%M:%S")
configure_file(buildtimestamp.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/buildtimestamp.cpp 
  ESCAPE_QUOTES @ONLY)
list(APPEND service_SOURCES buildtimestamp.hpp ${CMAKE_CURRENT_BINARY_DIR}/buildtimestamp.cpp)
### ^^^ add build date and time ^^^

### vvv git hash functionality vvv
if (GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    RESULT_VARIABLE result
    OUTPUT_VARIABLE service_git_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  if (result)
    set(service_git_hash "")
  endif()
else()
  set(service_git_hash "")
endif()
configure_file(githash.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/githash.cpp 
  ESCAPE_QUOTES @ONLY)
list(APPEND service_SOURCES githash.hpp ${CMAKE_CURRENT_BINARY_DIR}/githash.cpp)
### ^^^ git hash functionality ^^^


add_customer_sources_with_default(service service_SOURCES
  customer/program.{CUSTOMER}.cpp)

define_module(LIBRARY service=${service_VERSION}
  DEPENDS utility>=1.42 dbglog>=1.7
  Boost_FILESYSTEM Boost_PROGRAM_OPTIONS Boost_SYSTEM
  ${service_EXTRA_DEPENDS}
  # we need pthread_* stuff
  THREADS)

add_library(service STATIC ${service_SOURCES})
buildsys_library(service)

target_link_libraries(service ${MODULE_LIBRARIES})
target_compile_definitions(service PRIVATE ${MODULE_DEFINITIONS})

add_subdirectory(tools EXCLUDE_FROM_ALL)
