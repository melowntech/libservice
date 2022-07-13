if(READLINE_FOUND AND NOT WIN32)
  define_module(BINARY service-ctrlclient=${service_VERSION}
    DEPENDS service=${service_VERSION}
    READLINE
    )

  set(service-ctrlclient_SOURCES
    ctrlclient.cpp
    )

  add_executable(service-ctrlclient ${service-ctrlclient_SOURCES})
  buildsys_binary(service-ctrlclient)

  target_link_libraries(service-ctrlclient ${MODULE_LIBRARIES})
  target_compile_definitions(service-ctrlclient PRIVATE ${MODULE_DEFINITIONS})

  set(service-netctrlclient_SOURCES
    netctrlclient.cpp
    )

  add_executable(service-netctrlclient ${service-netctrlclient_SOURCES})
  buildsys_binary(service-netctrlclient)

  target_link_libraries(service-netctrlclient ${MODULE_LIBRARIES})
  target_compile_definitions(service-netctrlclient PRIVATE ${MODULE_DEFINITIONS})
endif()
