add_executable(
    Logger
    Logger.cpp
)

target_link_libraries(
    Logger
    ${CMAKE_THREAD_LIBS_INIT}
    rt
)

install(TARGETS Logger DESTINATION ./)
