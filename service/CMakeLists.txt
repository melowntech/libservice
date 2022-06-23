# bump version here
set(service_VERSION 1.9)

define_module(LIBRARY service=${service_VERSION}
  DEPENDS utility>=1.41 dbglog>=1.7
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


### vvv git hash functionality vvv

if (GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    RESULT_VARIABLE result
    OUTPUT_VARIABLE git_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  if (result)
    set(git_hash "")
  endif()
else()
  set(git_hash "")
endif()

set(githash_in githash.hpp.in)
set(githash_out ${CMAKE_CURRENT_BINARY_DIR}/githash.hpp)

configure_file(${githash_in} ${githash_out}
  ESCAPE_QUOTES @ONLY)

list(APPEND service_SOURCES ${githash_out})

### ^^^ git hash functionality ^^^


add_customer_sources_with_default(service service_SOURCES
  customer/program.{CUSTOMER}.cpp)

add_library(service STATIC ${service_SOURCES})
buildsys_library(service)

target_link_libraries(service ${MODULE_LIBRARIES})
target_compile_definitions(service PRIVATE ${MODULE_DEFINITIONS})

add_subdirectory(tools EXCLUDE_FROM_ALL)
