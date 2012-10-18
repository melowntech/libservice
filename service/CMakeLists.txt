depends(service dbglog Boost_PROGRAM_OPTIONS)

set(MODULE_SERVICE_FOUND TRUE PARENT_SCOPE)

set(service_SOURCES
    program.cpp service.cpp cmdline.cpp
)

add_library(service STATIC ${service_SOURCES})
