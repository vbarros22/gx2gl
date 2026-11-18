cmake_minimum_required(VERSION 3.13)
project(gx2gl_gs_test C CXX)

# Configure with devkitPro's Wii U toolchain and a CafeGLSL build with geometry
# shader support, e.g.:
#   cmake -S examples/gl-gs-test -B build-test \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
#       -DGX2GL_CAFEGLSL_ROOT=<CafeGLSL checkout with build-cafe/>
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." gx2gl)

add_executable(gx2gl_gs_test main.cpp)
set_target_properties(gx2gl_gs_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(gx2gl_gs_test PRIVATE gl33_gx2_core)

wut_create_rpx(gx2gl_gs_test)
wut_create_wuhb(gx2gl_gs_test
   NAME "gx2gl GS test"
   SHORTNAME "gx2gl GS"
   AUTHOR "gx2gl")
