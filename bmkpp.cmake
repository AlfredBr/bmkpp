# Standalone entry point, or include() this file in an existing CMake project.
add_library(bmkpp INTERFACE)
add_library(bmkpp::bmkpp ALIAS bmkpp)
target_include_directories(bmkpp INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(bmkpp INTERFACE cxx_std_17)
