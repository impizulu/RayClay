# ==============================================================================
#  RayClay - one command: cmake -B build && cmake --build build --parallel 4
#
#  Builds the vendored patched GLFW, then librayclay as a static library from a
#  generated two-line implementation TU over the single rayclay.h. Consume via
#  add_subdirectory() and link rayclay::rayclay. On a checkout that carries
#  examples/ (the 'examples' branch), the examples build too.
# ==============================================================================
cmake_minimum_required(VERSION 3.21)

# Refuse an in-source build - it scatters CMakeCache.txt / CMakeFiles/ and the
# GLFW binary tree through the source. Checked before project() so the configure
# aborts before writing the cache; the REALPATH compare also catches `cmake -B .`
# and a symlinked root.
get_filename_component(_rc_src_real "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(_rc_bin_real "${CMAKE_BINARY_DIR}" REALPATH)
if(_rc_src_real STREQUAL _rc_bin_real)
    message(FATAL_ERROR
        "In-source builds are not allowed. Build out-of-source instead, e.g.\n"
        "    cmake -B build && cmake --build build --parallel 4\n"
        "Remove the stray CMakeCache.txt + CMakeFiles/ this attempt just created first.")
endif()

# rayclay.h is the single source of truth for the version; parse it so a release
# never needs to edit this file.
foreach(_part MAJOR MINOR PATCH)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/rayclay.h" _rc_line
         REGEX "^#define[ \t]+RC_VERSION_${_part}[ \t]+[0-9]+")
    if(NOT _rc_line)
        message(FATAL_ERROR "rayclay.h: '#define RC_VERSION_${_part} <n>' not found")
    endif()
    string(REGEX MATCH "[0-9]+" _rc_ver_${_part} "${_rc_line}")
endforeach()

project(rayclay
    VERSION ${_rc_ver_MAJOR}.${_rc_ver_MINOR}.${_rc_ver_PATCH}
    LANGUAGES C)
message(STATUS "RayClay ${PROJECT_VERSION}")

set(OpenGL_GL_PREFERENCE GLVND)
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Editors resolve includes - system headers included - from a compile database,
# not from this file. Without one an editor falls back to guessing and marks
# every #include as unresolvable while the build itself is perfectly fine. The
# presets set this too, but only some of them, and not the plain
# `cmake -S . -B build` path. Top-level only, for the same reason as the
# build-type default above: a consumer embedding RayClay owns that decision for
# their own tree, and a library forcing a database on them would overreach.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# RC_SIZE_OPT - the size-first build posture, opt-in and OFF by default.
#
# WHY IT IS HERE AND NOT ONLY IN THE FLAGS BELOW. Section-GC is unconditional a
# few lines down because a single-object library is useless without it. This
# option is what remains once that is already on: link-time optimisation across
# the whole tree, and STBTT_STATIC, which turns the vendored font rasteriser's
# ~200 external symbols into file-statics so the linker may inline and discard
# them. Measured on the shipped header, gcc 15.3.1, Release, x86-64, on the
# do-nothing app: 833,536 -> 783,248 bytes stripped, a saving of 50,288 (6.0%).
#
# It is applied DIRECTORY-WIDE and BEFORE add_subdirectory(glfw), deliberately:
# most of the bytes are in the vendored window library, not in rayclay's own
# object, and a posture set after the subdirectory would miss them entirely.
#
# LTO is PROBED, never assumed. check_ipo_supported answers for this compiler
# and generator, and a toolchain without it gets the rest of the posture plus a
# line saying so - an option that silently delivers less than it promises is
# worse than one that refuses, because it is believed.
#
# MSVC already links /OPT:REF,ICF and compiles /Gy, which is why the GNU/Clang
# section flags are skipped there. STBTT_STATIC and LTO are not applied on that
# toolchain, and this says so rather than no-op an option a developer set.
option(RC_SIZE_OPT "Aggressive binary-size build posture (LTO + section GC + static stb_truetype)" OFF)

if(RC_SIZE_OPT AND NOT MSVC)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_compile_definitions(STBTT_STATIC)
    if(APPLE)
        add_link_options(-Wl,-dead_strip)
    else()
        add_link_options(-Wl,--gc-sections)
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _rc_ipo_ok OUTPUT _rc_ipo_msg)
    if(_rc_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(STATUS "RC_SIZE_OPT: LTO unavailable (${_rc_ipo_msg}); section GC + static stb only.")
    endif()
elseif(RC_SIZE_OPT AND MSVC)
    message(STATUS "RC_SIZE_OPT: MSVC already section-GCs by default; STBTT_STATIC "
                   "and LTO are not applied on this toolchain.")
endif()

# Desktop: system OpenGL + the vendored GLFW (patched - a system GLFW silently
# loses Wayland titlebar dragging). On web, emscripten supplies the GLFW shim
# (-sUSE_GLFW=3) and WebGL2, so neither is built here. GLFW_BUILD_X11 /
# GLFW_BUILD_WAYLAND remain consumer-settable for a single-backend Linux trim.
if(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)  # defaults ON upstream
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(glfw)
endif()

# The library IS the header; generate the one implementation TU so no consumer
# ever writes it by hand.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rayclay_impl.c"
     "#define RAYCLAY_IMPLEMENTATION\n#include \"rayclay.h\"\n")
add_library(rayclay STATIC "${CMAKE_CURRENT_BINARY_DIR}/rayclay_impl.c")
add_library(rayclay::rayclay ALIAS rayclay)
target_include_directories(rayclay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The implementation TU needs C11 (with the toolchain's default extensions);
# consumers including rayclay.h need only C99. /utf-8: the sources hold UTF-8
# string literals MSVC would otherwise decode in the system codepage.
set_target_properties(rayclay PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(rayclay PRIVATE /utf-8)
endif()

# Section-GC, on by default: the library is ONE object file, so without it every
# consumer binary would carry the whole framework. MSVC Release already links
# with /OPT:REF,ICF.
if(NOT MSVC)
    target_compile_options(rayclay PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(rayclay INTERFACE
        $<IF:$<PLATFORM_ID:Darwin>,-Wl$<COMMA>-dead_strip,-Wl$<COMMA>--gc-sections>)
endif()

# A dependency-only interface target: the platform libraries the single header's
# implementation links against, WITHOUT the implementation itself. The library
# links it PUBLIC (so consumers inherit them); the differential tests on the
# 'test' branch also link it, because each test compiles its own copy of the
# implementation and so cannot link librayclay, yet needs the same libraries to
# resolve the window/titlebar backend it pulls in. Section-GC drops that code on
# ELF but not on PE, so on Windows the tests genuinely need these to link.
add_library(rayclay_deps INTERFACE)

if(NOT EMSCRIPTEN)
    target_link_libraries(rayclay_deps INTERFACE glfw OpenGL::GL)

    if(WIN32)
        # The titlebar backend names these with #pragma comment(lib,...), which
        # only MSVC honours; listing them keeps MinGW/clang links working.
        target_link_libraries(rayclay_deps INTERFACE comctl32 dwmapi shell32 user32 gdi32)
    endif()

    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)  # the titlebar's X11 backend calls Xlib directly
        target_link_libraries(rayclay_deps INTERFACE ${X11_LIBRARIES} m ${CMAKE_DL_LIBS})
    endif()
    # macOS: Cocoa/IOKit/CoreFoundation flow transitively from the glfw target.
endif()

target_link_libraries(rayclay PUBLIC rayclay_deps)

# Implementation-only compile definitions: they matter only when compiling the
# single header's implementation, so they stay PRIVATE - a consumer that merely
# includes rayclay.h never sees them, while the 'test' branch copies them onto
# its test targets so those compile the implementation exactly as the library.
if(NOT EMSCRIPTEN)
    if(MINGW)
        # msvcrt printf has no %zu; select MinGW's ISO-conforming stdio instead.
        target_compile_definitions(rayclay PRIVATE __USE_MINGW_ANSI_STDIO=1)
    endif()

    if(UNIX AND NOT APPLE)
        # Mirror GLFW's selected backends into the titlebar glue: a GLFW built
        # single-backend must not pull the absent backend's native accessors.
        # Default ON (both) if a replaced GLFW does not define them - fail safe.
        set(_rc_glfw_x11 ON)
        set(_rc_glfw_wayland ON)
        if(DEFINED GLFW_BUILD_X11)
            set(_rc_glfw_x11 ${GLFW_BUILD_X11})
        endif()
        if(DEFINED GLFW_BUILD_WAYLAND)
            set(_rc_glfw_wayland ${GLFW_BUILD_WAYLAND})
        endif()
        target_compile_definitions(rayclay PRIVATE
            RC_GLFW_X11=$<BOOL:${_rc_glfw_x11}>
            RC_GLFW_WAYLAND=$<BOOL:${_rc_glfw_wayland}>)
    endif()
endif()

# ------------------------------------------------------------------------------
#  Examples - present only on the 'examples' branch. On 'main' the sentinel below
#  is absent and this block is inert, so one CMakeLists serves both branches.
#
#  The sentinel is a source file rather than the directory, deliberately. A
#  directory test asks whether anything landed under examples/, which is weaker
#  than asking whether the sources this block enumerates are present. ci.yml
#  gates its example job the same way, on hashFiles('examples/ex00_hello/hello.c').
# ------------------------------------------------------------------------------
option(RC_BUILD_EXAMPLES "Build the RayClay examples" ${PROJECT_IS_TOP_LEVEL})

if(RC_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/ex00_hello/hello.c)
    add_executable(rayclay_ex00_hello                   examples/ex00_hello/hello.c)
    add_executable(rayclay_ex01_1980s_gui               examples/ex01_1980s_gui/main.c)
    add_executable(rayclay_ex02_1990s_gui               examples/ex02_1990s_gui/main.c)
    add_executable(rayclay_ex03_2000s_gui               examples/ex03_2000s_gui/main.c)
    add_executable(rayclay_ex04_2010s_gui               examples/ex04_2010s_gui/main.c)
    add_executable(rayclay_ex05_2020s_gui               examples/ex05_2020s_gui/main.c)
    add_executable(rayclay_ex10_rayclay_widgets_gallery examples/ex10_rayclay_widgets_gallery/main.c)
    # ex11 is the one example with no web arm: it SCANS A DIRECTORY and WRITES .h
    # files, which a browser page cannot do. Excluded rather than shipped broken.
    if(NOT EMSCRIPTEN)
        add_executable(rayclay_ex11_rayclay_icon_converter  examples/ex11_rayclay_icon_converter/main.c)
    endif()
    add_executable(rayclay_ex12_rayclay_inspector       examples/ex12_rayclay_inspector/main.c)
    # The ex20-ex29 band: fully functional demo apps, single-source like the
    # decade lineup rather than split like the bench suite (nothing benchmarks
    # them, so there is no reason for a second TU).
    add_executable(rayclay_ex20_system_monitor          examples/ex20_system_monitor/main.c)
    add_executable(rayclay_ex21_data_explorer           examples/ex21_data_explorer/main.c)
    # ex22 loads three real typefaces at runtime from examples/assets/fonts/.
    # It needs NO injected defines: its source defaults RC_EX22_FONT_DIR and
    # RC_EX22_ICON to repo-root-relative paths, which is exactly this tree's
    # layout, so it runs from the repo root like ex10 does.
    add_executable(rayclay_ex22_docs_reader             examples/ex22_docs_reader/main.c)
    # ex23 virtualizes 100,000 rows from a generated dataset: single source, no
    # assets, no local header, so it needs nothing beyond the target.
    add_executable(rayclay_ex23_issue_inbox             examples/ex23_issue_inbox/main.c)
    # ex24 loads an .svg at RUNTIME and draws the same artwork beside its
    # generated-header twin. Its artwork is examples/assets/icons/, shipped
    # beside it, so it needs no injected define.
    add_executable(rayclay_ex24_svg_live                examples/ex24_svg_live/main.c)
    # The bench suite: production-shaped multi-file apps (GUI TU + thin runner).
    add_executable(rayclay_bench_messenger
                   examples/bench/messenger/main.c
                   examples/bench/messenger/messenger_app.c)
    add_executable(rayclay_bench_notes
                   examples/bench/notes/main.c
                   examples/bench/notes/notes_app.c)
    add_executable(rayclay_bench_trader
                   examples/bench/trader/main.c
                   examples/bench/trader/trader_app.c)
    add_executable(rayclay_bench_platformer
                   examples/bench/platformer/main.c
                   examples/bench/platformer/platformer_app.c)
    add_executable(rayclay_bench_gallery
                   examples/bench/gallery/main.c
                   examples/bench/gallery/gallery_app.c)
    add_executable(rayclay_bench_opsdash
                   examples/bench/opsdash/main.c
                   examples/bench/opsdash/opsdash_app.c)

    if(NOT EMSCRIPTEN)
        # Repo-relative on purpose: an absolute path would bake the build machine's
        # home directory into the binary. Run ex10 from the repo root.
        target_compile_definitions(rayclay_ex10_rayclay_widgets_gallery PRIVATE
            RC_DEMO_LOGO="examples/assets/logos/rayclay-logo-1024.png")

        # The converter uses portable C string fns; silence MSVC's "use strcpy_s".
        target_compile_definitions(rayclay_ex11_rayclay_icon_converter PRIVATE
            $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    else()
        # ----------------------------------------------------------------------
        #  The web arm. THE SAME SOURCES - no example has a web variant; what
        #  differs is the link, the page shell, and how runtime assets reach the
        #  program (a browser has no filesystem, so they are preloaded into a
        #  virtual /assets root at the SAME paths the desktop arm reads).
        # ----------------------------------------------------------------------
        set(RC_DEMO_PNG   "${CMAKE_CURRENT_SOURCE_DIR}/examples/assets/logos/rayclay-logo-1024.png")
        set(RC_EX22_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/assets/fonts")
        set(RC_WEB_SHELL  "${CMAKE_CURRENT_SOURCE_DIR}/examples/web/shell.html")

        # THE PAGE NAME IS SET AT BUILD TIME AND MUST NEVER BE A COPY-TIME RENAME:
        # emcc bakes the wasm filename into the generated .js, so renaming the
        # output afterwards leaves <name>.js fetching the OLD .wasm and the page
        # dies with a blank canvas and no error.
        function(rc_web_example target out)
            set_target_properties(${target} PROPERTIES OUTPUT_NAME ${out} SUFFIX ".html")
            # GROWABLE_ARRAYBUFFERS=0: emcc 6.0.2 turned it on by default alongside
            # ALLOW_MEMORY_GROWTH, backing heap views with a RESIZABLE ArrayBuffer,
            # which Chrome's WebGL rejects ("texSubImage2D: ... must not be
            # resizable") - the glyph-atlas upload then faults and every page
            # renders blank.
            target_link_options(${target} PRIVATE
                -sUSE_GLFW=3
                -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3
                -sALLOW_MEMORY_GROWTH=1 -sGROWABLE_ARRAYBUFFERS=0 -sSTACK_SIZE=8MB
                -sENVIRONMENT=web -sMALLOC=emmalloc
                --shell-file "${RC_WEB_SHELL}")
            # emcc bakes the shell into <out>.html at LINK time and CMake does not
            # track it as an input; relink when it changes.
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${RC_WEB_SHELL}")
        endfunction()

        rc_web_example(rayclay_ex05_2020s_gui               dashboard)
        rc_web_example(rayclay_ex00_hello                   hello)
        rc_web_example(rayclay_ex01_1980s_gui               gui1980s)
        rc_web_example(rayclay_ex02_1990s_gui               gui1990s)
        rc_web_example(rayclay_ex03_2000s_gui               gui2000s)
        rc_web_example(rayclay_ex04_2010s_gui               gui2010s)
        rc_web_example(rayclay_ex10_rayclay_widgets_gallery gallery)
        rc_web_example(rayclay_ex12_rayclay_inspector       inspector)
        rc_web_example(rayclay_ex20_system_monitor          sysmon)
        rc_web_example(rayclay_ex21_data_explorer           explorer)
        rc_web_example(rayclay_ex22_docs_reader             reader)
        rc_web_example(rayclay_ex23_issue_inbox             inbox)
        rc_web_example(rayclay_ex24_svg_live                svg)
        rc_web_example(rayclay_bench_messenger              messenger)
        rc_web_example(rayclay_bench_notes                  notes)
        rc_web_example(rayclay_bench_trader                 trader)
        rc_web_example(rayclay_bench_platformer             platformer)
        rc_web_example(rayclay_bench_gallery                photos)
        rc_web_example(rayclay_bench_opsdash                opsdash)

        # Load-bearing: `SHELL:` is required, not decoration. CMake de-duplicates repeated
        # identical link options, so N bare `--preload-file` tokens collapse to ONE
        # while all N paths survive - measured on the generated link.txt: 4
        # requested, 1 flag emitted, 4 paths emitted. The three orphaned paths then
        # reach emcc as bare arguments and it reports them as missing INPUT FILES,
        # which reads like an absent asset and is not. `SHELL:` is CMake's
        # documented escape and keeps each flag adjacent to its value.
        target_compile_definitions(rayclay_ex10_rayclay_widgets_gallery PRIVATE
            RC_DEMO_LOGO="/assets/rayclay-logo-1024.png")
        target_link_options(rayclay_ex10_rayclay_widgets_gallery PRIVATE
            "SHELL:--preload-file ${RC_DEMO_PNG}@/assets/rayclay-logo-1024.png")

        # The trailing slash matters: the source concatenates a filename onto it.
        target_compile_definitions(rayclay_ex22_docs_reader PRIVATE
            RC_EX22_FONT_DIR="/assets/fonts/"
            RC_EX22_ICON="/assets/rayclay-logo-1024.png")
        target_link_options(rayclay_ex22_docs_reader PRIVATE
            "SHELL:--preload-file ${RC_EX22_FONTS}/Lato-Regular.ttf@/assets/fonts/Lato-Regular.ttf"
            "SHELL:--preload-file ${RC_EX22_FONTS}/Lato-Bold.ttf@/assets/fonts/Lato-Bold.ttf"
            "SHELL:--preload-file ${RC_EX22_FONTS}/AdwaitaMono-Regular.ttf@/assets/fonts/AdwaitaMono-Regular.ttf"
            "SHELL:--preload-file ${RC_DEMO_PNG}@/assets/rayclay-logo-1024.png")

        # ex24 opens its .svg sources at RUN time, so the web arm must ship them.
        # Deliberately not a directory preload: examples/assets/icons/ also holds
        # the GENERATED .h headers and a licence, none of which ex24 opens, and a
        # .data bundle is downloaded by every visitor.
        set(RC_EX24_SVGS chart-column expand folder maximize minimize minus
                         panel-left panel-right settings shrink x)
        foreach(_svg IN LISTS RC_EX24_SVGS)
            target_link_options(rayclay_ex24_svg_live PRIVATE
                "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/assets/icons/${_svg}.svg@/assets/icons/${_svg}.svg")
        endforeach()
        target_compile_definitions(rayclay_ex24_svg_live PRIVATE
            RC_EX24_SVG_DIR="/assets/icons/")

        # Stage the demo hub as the build's index.html so a local build is the same
        # tree a deploy would publish. A COPY, never a rename of a generated page -
        # see the rc_web_example() note above.
        add_custom_target(rayclay_web_hub ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/examples/web/hub.html
                    ${CMAKE_CURRENT_BINARY_DIR}/index.html
            COMMENT "web: staging the demo hub as index.html")
    endif()

    # ONE list, so the web arm cannot drift from the desktop one by a target.
    # ex11 is appended rather than listed because it exists only off the web.
    set(_rc_examples
        rayclay_ex00_hello rayclay_ex01_1980s_gui rayclay_ex02_1990s_gui
        rayclay_ex03_2000s_gui rayclay_ex04_2010s_gui rayclay_ex05_2020s_gui
        rayclay_ex10_rayclay_widgets_gallery
        rayclay_ex12_rayclay_inspector rayclay_ex20_system_monitor
        rayclay_ex21_data_explorer rayclay_ex22_docs_reader
        rayclay_ex23_issue_inbox rayclay_ex24_svg_live
        rayclay_bench_messenger rayclay_bench_notes rayclay_bench_trader
        rayclay_bench_platformer rayclay_bench_gallery
        rayclay_bench_opsdash)
    if(NOT EMSCRIPTEN)
        list(APPEND _rc_examples rayclay_ex11_rayclay_icon_converter)
    endif()

    foreach(_ex IN LISTS _rc_examples)
        target_link_libraries(${_ex} PRIVATE rayclay)
        # examples/bench holds the bench suite's shared contract (bench_app.h);
        # examples/assets resolves the "icons/..." includes.
        target_include_directories(${_ex} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/bench
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/assets)
        target_compile_options(${_ex} PRIVATE $<$<C_COMPILER_ID:MSVC>:/utf-8>)
    endforeach()
else()
    # ------------------------------------------------------------------------------
    #  SAY SO. A branch without examples is a legitimate state - the published
    #  `main` carries the library alone and the demos live on the `examples`
    #  branch - but until this else() existed the build simply omitted them in
    #  silence. A contributor ran the first command the docs give them,
    #  `cmake --preset desktop && cmake --build --preset desktop`, got exit 0,
    #  and had NOTHING RUNNABLE: zero executables and a preset whose own
    #  displayName promises "library + examples".
    #  A silently degraded build is the worst of the three possible outcomes -
    #  worse than a hard failure, which at least tells you - so this converts it
    #  into one actionable sentence. It costs nothing on the branches that do
    #  carry examples, because the if() arm is unchanged.
    # ------------------------------------------------------------------------------
    if(RC_BUILD_EXAMPLES)
        message(STATUS
            "RayClay: no examples on this branch - building the library only. "
            "The demos live on the 'examples' branch: "
            "git clone --branch examples <this repo>")
    endif()
endif()

# ------------------------------------------------------------------------------
#  Differential tests - present only on the 'test' branch. Like the examples
#  block above, the EXISTS check keeps this inert on 'main'/'examples', so one
#  CMakeLists serves every branch. Build + run them from the repo root with:
#      cmake -B build && cmake --build build && ctest --test-dir build
# ------------------------------------------------------------------------------
option(RC_BUILD_TESTS "Build the RayClay differential tests" ${PROJECT_IS_TOP_LEVEL})

if(RC_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt
   AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(test)
endif()
