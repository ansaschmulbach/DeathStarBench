add_executable(
    PostStorageService
    PostStorageService.cpp
    ${THRIFT_GEN_CPP_DIR}/PostStorageService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    PostStorageService PRIVATE
    ${LIBMEMCACHED_INCLUDE_DIR}
    ${MONGOC_INCLUDE_DIRS}
	#    /usr/local/include/jaegertracing
)

# set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)

target_link_libraries(
    PostStorageService
    ${MONGOC_LIBRARIES}
    ${LIBMEMCACHED_LIBRARIES}
    nlohmann_json::nlohmann_json
    # ${THRIFT_STATIC_LIB}
	${THRIFT_LIB}
    # ${OPENSSL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
	# jaegertracing
)

# Set the linker flags to ensure static linking
# set_target_properties(PostStorageService PROPERTIES
# 	LINK_FLAGS "-static -Wl,--verbose"
# )

install(TARGETS PostStorageService DESTINATION ./)
