add_executable(
    ComposePostService
    ComposePostService.cpp
    ${THRIFT_GEN_CPP_DIR}/ComposePostService.cpp
    ${THRIFT_GEN_CPP_DIR}/PostStorageService.cpp
    ${THRIFT_GEN_CPP_DIR}/UserTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/UserService.cpp
    ${THRIFT_GEN_CPP_DIR}/UniqueIdService.cpp
    ${THRIFT_GEN_CPP_DIR}/MediaService.cpp
    ${THRIFT_GEN_CPP_DIR}/TextService.cpp
    ${THRIFT_GEN_CPP_DIR}/HomeTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    ComposePostService PRIVATE
	# /usr/local/include/jaegertracing
)

set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)

target_link_libraries(
    ComposePostService
	${THRIFT_STATIC_LIB}
	${OPENSSL_LIBRARIES}
    # ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    Boost::log
    Boost::log_setup
    # OpenSSL::SSL
	# /usr/local/lib/libjaegertracing.so
    # /usr/local/lib/libSimpleAmqpClient.so
)

# Set the linker flags to ensure static linking
set_target_properties(ComposePostService PROPERTIES
	LINK_FLAGS "-static -Wl,--verbose"
)

install(TARGETS ComposePostService DESTINATION ./)
