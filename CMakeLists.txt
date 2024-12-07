cmake_minimum_required(VERSION 3.20)
project(runluau LANGUAGES CXX C)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)

if(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNIX")
elseif(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWINDOWS")
endif()

include_directories(shared)


# Add Luau submodule as a subdirectory
add_subdirectory(Luau ${CMAKE_BINARY_DIR}/_deps/Luau)

# Include Luau directories (adjust as needed)
include_directories(Luau/Compiler/include)
include_directories(Luau/Ast/include)
include_directories(Luau/CodeGen/include)
include_directories(Luau/Common/include)

# Add your source files
add_executable(runluau main.cpp execute.cpp plugins.cpp)

# Link against Luau libraries
target_link_libraries(runluau Luau)
