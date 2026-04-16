cmake_minimum_required(VERSION 3.16)
project(ngs_mpi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Compiler warnings ----
if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS MSMPI_NO_DEPRECATE_20)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Find MPI ----
find_package(MPI REQUIRED)
message(STATUS "MPI CXX include: ${MPI_CXX_INCLUDE_DIRS}")
message(STATUS "MPI CXX libs:    ${MPI_CXX_LIBRARIES}")

# ---- Common source files (shared by main exe and tests) ----
set(COMMON_SOURCES
    src/graph.cpp
    src/leader_election.cpp
    src/dijkstra.cpp
    src/metrics.cpp
)

# ---- Main executable ----
add_executable(ngs_mpi src/main.cpp ${COMMON_SOURCES})
target_include_directories(ngs_mpi PRIVATE include)
target_link_libraries(ngs_mpi PRIVATE MPI::MPI_CXX)

# ---- Test executable ----
add_executable(ngs_tests tests/test_main.cpp ${COMMON_SOURCES})
target_include_directories(ngs_tests PRIVATE include)
target_link_libraries(ngs_tests PRIVATE MPI::MPI_CXX)

# ---- Install ----
install(TARGETS ngs_mpi DESTINATION bin)
