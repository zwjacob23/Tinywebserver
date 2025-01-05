cmake_minimum_required(VERSION 3.5.0)
project(test1 VERSION 0.1.0 LANGUAGES C CXX)





include_directories(/usr/bin/mysql)
add_executable(test1 buffer.cpp log.cpp sqlconnpool.cpp test.cpp)
target_link_libraries(test1 pthread)
target_link_libraries(${PROJECT_NAME}  libmysqlclient.so)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
