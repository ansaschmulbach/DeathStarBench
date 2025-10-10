add_executable(
    UrlShortenService
    UrlShortenService.cpp
    ${THRIFT_GEN_CPP_DIR}/UrlShortenService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    UrlShortenService PRIVATE
    ${LIBMEMCACHED_INCLUDE_DIR}
    ${MONGOC_INCLUDE_DIRS}
	# /usr/local/include/jaegertracing
)

set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)

target_link_libraries(
    UrlShortenService
    nlohmann_json::nlohmann_json
    ${MONGOC_LIBRARIES}
    ${LIBMEMCACHED_LIBRARIES}
    # ${THRIFT_LIB}
	${THRIFT_STATIC_LIB}
	${OPENSSL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
	# jaegertracing
)

# Set the linker flags to ensure static linking
# set_target_properties(UrlShortenService PROPERTIES
# 	LINK_FLAGS "-static -Wl,--verbose"
# )

install(TARGETS UrlShortenService DESTINATION ./)
