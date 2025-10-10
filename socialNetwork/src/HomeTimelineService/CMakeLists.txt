add_executable(
    HomeTimelineService
    HomeTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/HomeTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/PostStorageService.cpp
    ${THRIFT_GEN_CPP_DIR}/SocialGraphService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    HomeTimelineService PRIVATE
	#    /usr/local/include/jaegertracing
    /usr/local/include/hiredis
    /usr/local/include/sw
)

set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)

target_link_libraries(
    HomeTimelineService
    nlohmann_json::nlohmann_json
	${THRIFT_STATIC_LIB}
	${OPENSSL_LIBRARIES}
    # ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
    Boost::program_options
	#    jaegertracing
    /usr/local/lib/libhiredis.a
    /usr/local/lib/libhiredis_ssl.a
    /usr/local/lib/libredis++.a
    # OpenSSL::SSL
)

# Set the linker flags to ensure static linking
set_target_properties(HomeTimelineService PROPERTIES
	LINK_FLAGS "-static -Wl,--verbose"
)

install(TARGETS HomeTimelineService DESTINATION ./)
