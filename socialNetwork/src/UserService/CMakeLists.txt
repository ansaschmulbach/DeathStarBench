add_executable(
    UserService
    UserService.cpp
    ${THRIFT_GEN_CPP_DIR}/UserService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
    ${THRIFT_GEN_CPP_DIR}/SocialGraphService.cpp
)

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pic -fno-plt")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pic -fno-plt")

target_include_directories(
    UserService PRIVATE
    ${LIBMEMCACHED_INCLUDE_DIR}
    ${MONGOC_INCLUDE_DIRS}
    /usr/local/include/jwt
	# /usr/local/include/jaegertracing
)

target_link_libraries(UserService PRIVATE
    ${MONGOC_LIBRARIES}
    ${LIBMEMCACHED_LIBRARIES} 
    nlohmann_json::nlohmann_json
    ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
	# jaegertracing
    OpenSSL::SSL
)

# set(OPENSSL_LIBRARIES /usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a)


# Set the linker flags to ensure static linking
# set_target_properties(UserService PROPERTIES
# 	LINK_FLAGS "-static "
# )

# target_link_options(UserService PRIVATE
#   "-static"
#   "-Wl,--whole-archive"
# )
# target_link_libraries(UserService PRIVATE
#   "-static"
#   "-Wl,--whole-archive"
#     ${LIBMEMCACHEDCORE_LIBRARY} 
#     # ${LIBMEMCACHEDUTIL_LIBRARY}
# /usr/local/lib/libbson-static-1.0.a
# 	${THRIFT_STATIC_LIB}
#     ${MONGOC_LIBRARIES}
# )
# # target_link_options(UserService PRIVATE
# #   "-Wl,--no-whole-archive"
# # )
# 
# target_link_libraries(UserService PRIVATE
#    "-Wl,--no-whole-archive"
#     # ${LIBMEMCACHED_LIBRARIES}
#     nlohmann_json::nlohmann_json
# 	${OPENSSL_LIBRARIES}
#     # ${THRIFT_LIB}
#     ${CMAKE_THREAD_LIBS_INIT}
#     ${Boost_LIBRARIES}
#     Boost::log
#     Boost::log_setup
# 	# jaegertracing
#     # OpenSSL::SSL
# )

# target_link_libraries(UserService PRIVATE
#   sasl2 ldap lber gnutls gcrypt gpg-error resolv gmp nettle hogweed tasn1 pthread ssl crypto
#   boost_log boost_log_setup boost_program_options boost_chrono boost_filesystem boost_regex boost_thread boost_atomic
# )

install(TARGETS UserService DESTINATION ./)
