cmake_minimum_required(VERSION 3.22)

project(hpvis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WITH_NAMASTER "Build with NaMaster support" ON)
option(HPVIS_IPO_SUPPORTED "Enable interprocedural optimization (IPO/LTO) if supported" ON)

# Default to a Release build when the user didn't pick a config.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()



set(HEALPIX_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/healpix)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Concurrent Graphs Svg)
find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


# Bundled copy is not built/installed by default; we link the system cfitsio
# below (installing it would overwrite the system library).
add_subdirectory(thirdparty/cfitsio EXCLUDE_FROM_ALL)
pkg_check_modules(CFITSIO REQUIRED IMPORTED_TARGET cfitsio)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

# --- HEALPix C++ (and libsharp) ----------------------------------------
# Built from the vendored sources into the build tree (out of tree, so the
# sources stay clean and no absolute paths end up in the repository).
include(ExternalProject)
set(HEALPIX_PREFIX ${CMAKE_BINARY_DIR}/healpix)
file(MAKE_DIRECTORY ${HEALPIX_PREFIX}/include ${HEALPIX_PREFIX}/include/healpix_cxx)

# The spherical harmonic transforms dominate the power spectrum, so build the
# libraries with -O3 (their configure otherwise defaults to -g -O2; it adds
# -fopenmp itself)
set(HEALPIX_OPT_FLAGS "-O3")

ExternalProject_Add(libsharp_ext
    PREFIX ${CMAKE_BINARY_DIR}/libsharp
    SOURCE_DIR ${HEALPIX_ROOT}/src/common_libraries/libsharp
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "CFLAGS=${HEALPIX_OPT_FLAGS}"
        <SOURCE_DIR>/configure --prefix=${HEALPIX_PREFIX}
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install
    BUILD_BYPRODUCTS ${HEALPIX_PREFIX}/lib/libsharp.so)

ExternalProject_Add(healpix_cxx_ext
    PREFIX ${CMAKE_BINARY_DIR}/healpix_cxx
    SOURCE_DIR ${HEALPIX_ROOT}/src/cxx
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "PKG_CONFIG_PATH=${HEALPIX_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
        "CFLAGS=${HEALPIX_OPT_FLAGS}"
        "CXXFLAGS=${HEALPIX_OPT_FLAGS}"
        <SOURCE_DIR>/configure --prefix=${HEALPIX_PREFIX}
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install
    BUILD_BYPRODUCTS ${HEALPIX_PREFIX}/lib/libhealpix_cxx.so
    DEPENDS libsharp_ext)

add_library(healpix_cxx SHARED IMPORTED)
set_target_properties(healpix_cxx PROPERTIES
    IMPORTED_LOCATION ${HEALPIX_PREFIX}/lib/libhealpix_cxx.so
    INTERFACE_INCLUDE_DIRECTORIES "${HEALPIX_PREFIX}/include;${HEALPIX_PREFIX}/include/healpix_cxx"
    INTERFACE_LINK_LIBRARIES "${HEALPIX_PREFIX}/lib/libsharp.so;PkgConfig::CFITSIO")
add_dependencies(healpix_cxx healpix_cxx_ext)

# --- NaMaster (libnmt) -------------------------------------------------
# libnmt needs the HEALPix C library (chealpix), which isn't packaged on
# most systems, so build it from the vendored HEALPix sources first.
if (WITH_NAMASTER)
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
endif()


add_library(nmt SHARED IMPORTED)
set_target_properties(nmt PROPERTIES
    IMPORTED_LOCATION ${NMT_PREFIX}/lib/libnmt.so
    INTERFACE_INCLUDE_DIRECTORIES ${NMT_PREFIX}/include)

if (WITH_NAMASTER)
    add_dependencies(nmt namaster_ext)
endif()

# --- CLASS (Boltzmann code, for theory spectra from cosmological parameters) --
# Built from thirdparty/class_public the way its Makefile does: C sources, plus
# the few files that are C++ in CLASS, with HyRec. CLASS reads its data files
# (BBN, HyRec) from CLASS_DATA_DIR at run time.
set(CLASS_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/class_public)
set(CLASS_DATA_DIR ${CLASS_ROOT} CACHE PATH "Directory with CLASS's external/ data files")
set(CLASS_C_SOURCES
    tools/growTable.c tools/dei_rkck.c tools/sparse.c tools/evolver_rkck.c
    tools/evolver_ndf15.c tools/parser.c tools/quadrature.c tools/common.c
    tools/trigonometric_integrals.c
    source/input.c source/background.c source/thermodynamics.c
    source/fourier.c source/distortions.c
    external/RecfastCLASS/wrap_recfast.c
    external/heating/injection.c external/heating/noninjection.c
    external/HyRec2020/hyrectools.c external/HyRec2020/helium.c
    external/HyRec2020/hydrogen.c external/HyRec2020/history.c
    external/HyRec2020/wrap_hyrec.c external/HyRec2020/energy_injection.c
    external/Halofit/halofit.c)
set(CLASS_CXX_SOURCES
    tools/arrays.c tools/hyperspherical.c
    source/perturbations.c source/primordial.c source/transfer.c
    source/harmonic.c source/lensing.c external/HMcode/hmcode.c)
list(TRANSFORM CLASS_C_SOURCES PREPEND ${CLASS_ROOT}/)
list(TRANSFORM CLASS_CXX_SOURCES PREPEND ${CLASS_ROOT}/)
set_source_files_properties(${CLASS_CXX_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(class_lib STATIC ${CLASS_C_SOURCES} ${CLASS_CXX_SOURCES})
target_include_directories(class_lib SYSTEM PUBLIC
    ${CLASS_ROOT}/include ${CLASS_ROOT}/external/RecfastCLASS
    ${CLASS_ROOT}/external/heating ${CLASS_ROOT}/external/HyRec2020
    ${CLASS_ROOT}/external/Halofit ${CLASS_ROOT}/external/HMcode)
target_compile_definitions(class_lib PRIVATE HYREC
    __CLASSDIR__="${CLASS_DATA_DIR}")
target_compile_options(class_lib PRIVATE -w -O3 -fPIC
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
set_target_properties(class_lib PROPERTIES CXX_STANDARD 11
    POSITION_INDEPENDENT_CODE ON)
target_link_libraries(class_lib PUBLIC OpenMP::OpenMP_C OpenMP::OpenMP_CXX m)

include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/hpvis.cpp
    src/view.cpp
    src/fitsinfo.cpp
    src/npyreader.cpp
    src/recentfiles.cpp
    src/projectionmath.cpp
    src/appsettings.cpp
    src/classwrap.cpp
    src/colormapregistry.cpp
    src/skyframe.cpp
    src/mapexpression.cpp
    src/mapoperation.cpp
    src/histogram.cpp
    src/mapoperationdialog.cpp
    src/resolution.cpp
    src/theoryspectrum.cpp
    src/smoothing.cpp
    src/smoothdialog.cpp
    src/multipolefilter.cpp
    src/multipolefilterdialog.cpp
    src/harmonicmaps.cpp
    src/crossspectrum.cpp
    src/harmonicdialog.cpp
    src/masksource.cpp
    src/fileexplorerdock.cpp
    src/masterspectrum.cpp
    src/masksourcedialog.cpp
    src/downgradedialog.cpp
    src/rotationdialog.cpp
    src/unitsdialog.cpp
    src/randommap.cpp
    src/randommapdialog.cpp
    src/units.cpp
    src/exportdialog.cpp
    src/fitsheaderdialog.cpp
    src/openglwidget.cpp
    src/tilecache.cpp
    src/tabbar.cpp
    src/tabwidget.cpp
    src/viewtoolbar.cpp
    src/collapsiblesection.cpp
    src/shaderloader.cpp
    src/statusbar.cpp
    src/settingsdialog.cpp
    src/aboutbanner.cpp
    src/aboutcontent.cpp
    src/usagedialog.cpp
    src/powerspectrumdock.cpp
    src/plotrenderer.cpp
    src/statisticsdock.cpp

    src/qml/powerspectrumplot.qml

    resources.qrc

    include/npyreader.hpp
    include/settingsdialog.hpp
    include/aboutinfo.hpp
    include/aboutbanner.hpp
    include/aboutcontent.hpp
    include/usagedialog.hpp
    include/exportoptions.hpp
    include/view.hpp
    include/hpvis.hpp
    include/colormapregistry.hpp
    include/skyframe.hpp
    include/mapexpression.hpp
    include/mapoperation.hpp
    include/histogram.hpp
    include/mapoperationdialog.hpp
    include/resolution.hpp
    include/theoryspectrum.hpp
    include/smoothing.hpp
    include/smoothdialog.hpp
    include/multipolefilter.hpp
    include/multipolefilterdialog.hpp
    include/harmonicmaps.hpp
    include/crossspectrum.hpp
    include/harmonicdialog.hpp
    include/masksource.hpp
    include/masksourcedialog.hpp
    include/downgradedialog.hpp
    include/rotationdialog.hpp
    include/unitsdialog.hpp
    include/randommap.hpp
    include/randommapdialog.hpp
    include/units.hpp
    include/exportdialog.hpp
    include/openglwidget.hpp
    include/tilecache.hpp
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
    include/cosmoparams.hpp
    include/classwrap.hpp
    include/fitsheaderdialog.hpp
    include/statusbar.hpp
    include/powerspectrumdock.hpp
    include/plotrenderer.hpp
    include/statisticsdock.hpp
    include/fileexplorerdock.hpp
    include/masterspectrum.hpp
)

# So the executable finds the vendored .so at runtime
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${HEALPIX_PREFIX}/lib;${NMT_PREFIX}/lib")

target_link_libraries(${PROJECT_NAME} PRIVATE
    healpix_cxx
    class_lib
    PkgConfig::CFITSIO
    PkgConfig::LIBZIP
    nmt
    OpenMP::OpenMP_CXX
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Graphs
    Qt6::Svg
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

    if (HPVIS_IPO_SUPPORTED)
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

if(HPVIS_IPO_SUPPORTED)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Man page: generated from docs/MAN.md with pandoc (the version in the footer
# comes from the project) and installed to man1
find_program(PANDOC_EXECUTABLE pandoc)
if(PANDOC_EXECUTABLE)
    set(HPVIS_MANPAGE ${CMAKE_BINARY_DIR}/hpvis.1)
    add_custom_command(
        OUTPUT ${HPVIS_MANPAGE}
        COMMAND ${PANDOC_EXECUTABLE} -s -f markdown-smart -t man
                -M "footer=${PROJECT_NAME} ${PROJECT_VERSION}"
                ${CMAKE_SOURCE_DIR}/docs/MAN.md -o ${HPVIS_MANPAGE}
        DEPENDS ${CMAKE_SOURCE_DIR}/docs/MAN.md
        COMMENT "Generating man page"
        VERBATIM)
    add_custom_target(manpage ALL DEPENDS ${HPVIS_MANPAGE})
    install(FILES ${HPVIS_MANPAGE}
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
else()
    message(STATUS "pandoc not found: man page will not be installed")
endif()

# Install the vendored shared libraries next to the executable's prefix so the
# installed binary can find them (build-tree RPATH is dropped on install).
install(DIRECTORY ${HEALPIX_PREFIX}/lib/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/hpvis
    FILES_MATCHING PATTERN "libhealpix_cxx.so*" PATTERN "libsharp.so*")
if(WITH_NAMASTER)
    install(DIRECTORY ${NMT_PREFIX}/lib/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/hpvis
        FILES_MATCHING PATTERN "libnmt.so*")
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/hpvis")
