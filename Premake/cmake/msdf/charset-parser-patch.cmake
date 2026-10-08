# includeの失敗を伝播する修正を構築先で適用する
function(nem_patch_msdf_charset target original)
    file(READ "${original}" source)
    string(REGEX MATCHALL "INCLUDE\\(userData, buffer\\)" calls "${source}")
    list(LENGTH calls count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "msdf charset include handler changed; review the local patch")
    endif()
    string(FIND "${source}" "INCLUDE(userData, buffer);" position)
    if(NOT position EQUAL -1)
        string(REPLACE "INCLUDE(userData, buffer);"
            "if (!INCLUDE(userData, buffer))\n                        return false;" patched "${source}")
    else()
        string(FIND "${source}" "if (!INCLUDE(userData, buffer))" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "msdf charset include handler changed; review the local patch")
        endif()
        set(patched "${source}")
    endif()

    # 循環参照と深すぎるincludeを読込中の範囲で拒否する
    set(signature "bool Charset::load(const char *filename, bool disableCharLiterals) {")
    string(REGEX MATCHALL "bool Charset::load\\(const char \\*filename, bool disableCharLiterals\\) \\{" loads "${patched}")
    list(LENGTH loads load_count)
    if(NOT load_count EQUAL 1)
        message(FATAL_ERROR "msdf charset load entry changed; review the local patch")
    endif()
    set(guard [=[
    // 循環includeと深すぎる参照を拒否する
    constexpr size_t kMaxIncludeDepth = 64;
    static thread_local std::vector<std::filesystem::path> activePaths;
    if (activePaths.size() >= kMaxIncludeDepth)
        return false;
    std::error_code error;
    std::filesystem::path path = std::filesystem::weakly_canonical(std::filesystem::path(filename), error);
    if (error)
        return false;
    for (const std::filesystem::path &active : activePaths) {
        if (std::filesystem::equivalent(active, path, error) || error)
            return false;
    }
    activePaths.push_back(std::move(path));
    // 成否にかかわらず、この読込の履歴を戻す
    struct LoadScope {
        std::vector<std::filesystem::path> &paths;
        ~LoadScope() { paths.pop_back(); }
    } scope = { activePaths };
]=])
    string(REPLACE "${signature}" "${signature}\n${guard}" patched "${patched}")
    string(REPLACE "#include <string>" "#include <string>\n#include <filesystem>\n#include <utility>\n#include <vector>"
        patched "${patched}")

    # 内容が同じなら生成物の更新時刻を変えない
    set(directory "${CMAKE_BINARY_DIR}/NEMPatches/msdf-atlas-gen")
    file(MAKE_DIRECTORY "${directory}")
    set(output "${directory}/charset-parser.cpp")
    set(previous "")
    if(EXISTS "${output}")
        file(READ "${output}" previous)
    endif()
    if(NOT previous STREQUAL patched)
        file(WRITE "${output}" "${patched}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")

    # 元のParserを構築対象から外し、修正済みの一つだけを使う
    get_target_property(source_directory ${target} SOURCE_DIR)
    get_target_property(sources ${target} SOURCES)
    set(updated)
    set(replaced 0)
    foreach(entry IN LISTS sources)
        get_filename_component(absolute "${entry}" ABSOLUTE BASE_DIR "${source_directory}")
        if(absolute STREQUAL original)
            list(APPEND updated "${output}")
            math(EXPR replaced "${replaced} + 1")
        else()
            list(APPEND updated "${absolute}")
        endif()
    endforeach()
    if(NOT replaced EQUAL 1)
        message(FATAL_ERROR "msdf charset source ownership changed; review the local patch")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${updated}")
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    get_filename_component(headers "${original}" DIRECTORY)
    target_include_directories(${target} PRIVATE "${headers}")
endfunction()
