cmake_minimum_required(VERSION 3.22)

project(Hpixview VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Default to a Release build when the user didn't pick a config.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()



# Point pkg-config at the vendored HEALPix
set(HEALPIX_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/healpix)
set(ENV{PKG_CONFIG_PATH} "${HEALPIX_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Concurrent Graphs)
find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


add_subdirectory(thirdparty/cfitsio)
pkg_check_modules(HEALPIX REQUIRED IMPORTED_TARGET healpix_cxx)
pkg_check_modules(CFITSIO REQUIRED IMPORTED_TARGET cfitsio)

# --- NaMaster (libnmt) -------------------------------------------------
# libnmt needs the HEALPix C library (chealpix), which isn't packaged on
# most systems, so build it from the vendored HEALPix sources first.
set(CHEALPIX_SRC_DIR ${CMAKE_SOURCE_DIR}/thirdparty/healpix/src/C/subs)
add_library(chealpix STATIC ${CHEALPIX_SRC_DIR}/chealpix.c)
set_target_properties(chealpix PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/chealpix/lib)
target_include_directories(chealpix PUBLIC ${CHEALPIX_SRC_DIR})
target_link_libraries(chealpix PUBLIC PkgConfig::CFITSIO)
target_compile_options(chealpix PRIVATE -w)

# libnmt's autotools build only works in-source, so build a copy of the
# submodule inside the build tree using its own install_libnmt.sh.
include(ExternalProject)
set(NMT_PREFIX ${CMAKE_BINARY_DIR}/namaster/src/namaster_ext/_deps)
file(MAKE_DIRECTORY ${NMT_PREFIX}/include)
ExternalProject_Add(namaster_ext
    PREFIX ${CMAKE_BINARY_DIR}/namaster
    DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/thirdparty/NaMaster
        ${CMAKE_BINARY_DIR}/namaster/src/namaster_ext
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        "CPPFLAGS=-I${CHEALPIX_SRC_DIR}"
        "LDFLAGS=-L${CMAKE_BINARY_DIR}/chealpix/lib"
        bash scripts/install_libnmt.sh
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${NMT_PREFIX}/lib/libnmt.so
    DEPENDS chealpix)

add_library(nmt SHARED IMPORTED)
set_target_properties(nmt PROPERTIES
    IMPORTED_LOCATION ${NMT_PREFIX}/lib/libnmt.so
    INTERFACE_INCLUDE_DIRECTORIES ${NMT_PREFIX}/include)
add_dependencies(nmt namaster_ext)

include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/hpixview.cpp
    src/view.cpp
    src/fitsinfo.cpp
    src/recentfiles.cpp
    src/projectionmath.cpp
    src/appsettings.cpp
    src/fitsheaderdialog.cpp
    src/openglwidget.cpp
    src/tabbar.cpp
    src/tabwidget.cpp
    src/viewtoolbar.cpp
    src/collapsiblesection.cpp
    src/shaderloader.cpp
    src/statusbar.cpp
    src/settingsdialog.cpp
    src/powerspectrumdialog.cpp

    src/qml/powerspectrumplot.qml

    resources.qrc

    include/settingsdialog.hpp
    include/exportoptions.hpp
    include/view.hpp
    include/hpixview.hpp
    include/openglwidget.hpp
    include/argparse.hpp
    include/tabwidget.hpp
    include/tabbar.hpp
    include/viewtoolbar.hpp
    include/collapsiblesection.hpp
    include/shaderloader.hpp
    include/mapcamera.hpp
    include/parallel.hpp
    include/fitsinfo.hpp
    include/recentfiles.hpp
    include/projectionmath.hpp
    include/appsettings.hpp
    include/fitsheaderdialog.hpp
    include/statusbar.hpp
    include/powerspectrumdialog.hpp
)

# So the executable finds the vendored .so at runtime
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${HEALPIX_ROOT}/lib;${NMT_PREFIX}/lib")

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::HEALPIX
    PkgConfig::CFITSIO
    nmt
    OpenMP::OpenMP_CXX
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Graphs
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROJECT_NAME="${PROJECT_NAME}"
    PROJECT_VERSION="${PROJECT_VERSION}"
)

# Extra optimization flags on top of the build-type defaults.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG -fno-plt -ffunction-sections -fdata-sections>
        $<$<CONFIG:RelWithDebInfo>:-O3 -fno-plt -ffunction-sections -fdata-sections>
        -Wall -Wextra -Wpedantic -Werror -Wno-unused-return-value
    )

    if (HPIXVIEW_IPO_SUPPORTED)
	target_link_options(${PROJECT_NAME} PRIVATE
		$<$<CONFIG:Release>:-Wl,--gc-sections -Wl,-s>
		$<$<CONFIG:RelWithDebInfo>:-Wl,--gc-sections>
		)
endif()
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        /W4 /WX
    )
endif()

if(HPIXVIEW_IPO_SUPPORTED)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_SOURCE_DIR}/docs/hpixview.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

install(FILES ${CMAKE_SOURCE_DIR}/example_config.toml
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

