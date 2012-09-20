set(service_SOURCES
    program.cpp service.cpp cmdline.cpp
)

add_library(service STATIC ${service_SOURCES})
