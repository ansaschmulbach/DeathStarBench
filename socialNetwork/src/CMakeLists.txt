include("../cmake/Findthrift.cmake")

# NOTE: locally trimmed for a UniqueIdService-only native build (no Docker,
# no Mongo/AMQP on this host) -- not intended to be upstreamed.
find_package(nlohmann_json 3.5.0 REQUIRED)
find_package(Threads)
find_package(OpenSSL REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS log log_setup program_options)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  link_directories(${Boost_LIBRARY_DIRS})
endif()

#set(POCO_PREFIX      "/usr/local")
#set(POCO_INCLUDE_DIR "${POCO_PREFIX}/include")
#set(POCO_LIB_DIR     "${POCO_PREFIX}/lib")
#set(POCO_LIBS        "${POCO_LIB_DIR}/libPocoNet.so"
#                     "${POCO_LIB_DIR}/libPocoUtil.so"
#                     "${POCO_LIB_DIR}/libPocoFoundation.so")


set(THRIFT_GEN_CPP_DIR ../../gen-cpp)

add_subdirectory(UniqueIdService)
add_subdirectory(MediaService)
add_subdirectory(CombinedService)
add_subdirectory(Logger)
add_subdirectory(ScheduleLogger)
add_subdirectory(ScheduleReplay)
