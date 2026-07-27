cmake_minimum_required(VERSION 3.21)

project(qt_static_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt is located through CMAKE_PREFIX_PATH, e.g.
#   cmake -B build -DCMAKE_PREFIX_PATH=/path/to/static-qt
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network Sql)

qt_add_executable(qt_static_test
    src/main.cpp
    src/report.cpp
    src/t_buildinfo.cpp
    src/t_text.cpp
    src/t_datetime.cpp
    src/t_serialization.cpp
    src/t_files.cpp
    src/t_objects.cpp
    src/t_concurrency.cpp
    src/t_sql.cpp
    src/t_network.cpp
)

# Plain console application on every platform.
set_target_properties(qt_static_test PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

target_include_directories(qt_static_test PRIVATE src)

qt_add_resources(qt_static_test "testdata"
    PREFIX "/data"
    BASE "data"
    FILES
        data/greeting.txt
        data/sample.xml
)

target_link_libraries(qt_static_test PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
)