add_executable(
    MediaService
    MediaService.cpp
    ${THRIFT_GEN_CPP_DIR}/MediaService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    MediaService PRIVATE
	#    /usr/local/include/jaegertracing
)

set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)

target_link_libraries(
    MediaService
    nlohmann_json::nlohmann_json
	${THRIFT_STATIC_LIB}
	${OPENSSL_LIBRARIES}
    # ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
	#    jaegertracing
)

# Set the linker flags to ensure static linking
set_target_properties(MediaService PROPERTIES
	LINK_FLAGS "-static -Wl,--verbose"
)

install(TARGETS MediaService DESTINATION ./)
