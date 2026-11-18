cmake_minimum_required(VERSION 3.13)
project(my_gx2gl_app CXX)

# Build:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake
#   cmake --build build
# The .wuhb (for Aroma) and .rpx end up in build/.
set(GX2GL_SDK "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Path to the unpacked gx2gl SDK")

add_executable(my_gx2gl_app main.cpp)
set_target_properties(my_gx2gl_app PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(my_gx2gl_app PRIVATE "${GX2GL_SDK}/include")
target_link_libraries(my_gx2gl_app PRIVATE
   "${GX2GL_SDK}/lib/libgl33_gx2_core.a"
   "${GX2GL_SDK}/lib/libcafeglsl.a"
   wut stdc++ m)

wut_create_rpx(my_gx2gl_app)
wut_create_wuhb(my_gx2gl_app NAME "My gx2gl app" SHORTNAME "gx2gl app" AUTHOR "me")
