file(GLOB_RECURSE NVME_SOURCES "*.cpp")
list(FILTER NVME_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_library(nvme_core STATIC ${NVME_SOURCES})
add_executable(nvme_driver main.cpp)
target_link_libraries(nvme_driver PRIVATE nvme_core)

target_compile_features(nvme_core PUBLIC cxx_std_23)
set_target_properties(nvme_core nvme_driver PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(nvme_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_compile_options(nvme_core PUBLIC
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wformat=2
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(nvme_core PUBLIC
        -O3
        -march=native
    )

    # LTO check (relies on the root check)
    if(ipo_supported)
        set_target_properties(nvme_core nvme_driver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(nvme_core PUBLIC
        -O0
        -g3
        -DDEBUG
        -fsanitize=address,undefined
    )
    target_link_options(nvme_core PUBLIC -fsanitize=address,undefined)
endif()
