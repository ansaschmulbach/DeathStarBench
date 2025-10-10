add_executable(
    UserMentionService
    UserMentionService.cpp
    ${THRIFT_GEN_CPP_DIR}/UserMentionService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

target_include_directories(
    UserMentionService PRIVATE
    ${LIBMEMCACHED_INCLUDE_DIR}
    ${MONGOC_INCLUDE_DIRS}
	# /usr/local/include/jaegertracing
)

target_link_libraries(
    UserMentionService
    ${MONGOC_LIBRARIES}
    ${LIBMEMCACHED_LIBRARIES}
    nlohmann_json::nlohmann_json
	${THRIFT_STATIC_LIB}
	${OPENSSL_LIBRARIES}
    # ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
	# jaegertracing
)

# Set the linker flags to ensure static linking
# set_target_properties(UserMentionService PROPERTIES
# 	LINK_FLAGS "-static -Wl,--verbose"
# )

install(TARGETS UserMentionService DESTINATION ./)
