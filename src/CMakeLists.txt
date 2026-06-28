file(GLOB_RECURSE NVME_SOURCES "*.cpp")

add_executable(nvme_driver ${NVME_SOURCES})

target_compile_features(nvme_driver PRIVATE cxx_std_23)
set_target_properties(nvme_driver PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(nvme_driver PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_options(nvme_driver PRIVATE
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
    target_compile_options(nvme_driver PRIVATE
        -O3
        -march=native
    )

    # LTO check (relies on the root check)
    if(ipo_supported)
        set_target_properties(nvme_driver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(nvme_driver PRIVATE
        -O0
        -g3
        -DDEBUG
        -fsanitize=address,undefined
    )
    target_link_options(nvme_driver PRIVATE -fsanitize=address,undefined)
endif()
