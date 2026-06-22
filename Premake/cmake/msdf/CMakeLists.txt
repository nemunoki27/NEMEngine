cmake_minimum_required(VERSION 3.15)

# freetype/msdfgen/msdf-atlas-genをvcpkg無しのソースからまとめてビルドするラッパ
# PNG/SVG/Skiaは使わずfreetypeのみを実依存にする、PNG出力はエンジン側のDirectXTexで行う
project(nem_msdf LANGUAGES C CXX)

get_filename_component(EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../Project/Externals" ABSOLUTE)

# --- freetype 依存を全て切った最小構成 ---
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory("${EXTERNALS_ROOT}/freetype" "${CMAKE_BINARY_DIR}/freetype")

# msdfgenはFreetype::Freetypeターゲットを探すので、無ければエイリアスを張ってfind_packageを回避する
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# --- msdf-atlas-gen 内部でmsdfgenもビルドされる ---
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_INSTALL OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
add_subdirectory("${EXTERNALS_ROOT}/msdf-atlas-gen" "${CMAKE_BINARY_DIR}/msdf-atlas-gen")
