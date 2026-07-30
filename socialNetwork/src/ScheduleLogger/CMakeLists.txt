add_executable(
    ScheduleLogger
    ScheduleLogger.cpp
)

target_link_libraries(
    ScheduleLogger
    ${CMAKE_THREAD_LIBS_INIT}
    rt
)

install(TARGETS ScheduleLogger DESTINATION ./)
