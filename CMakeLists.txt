cmake_minimum_required(VERSION 3.5)

# set project's name
set(PROJECT luster-ko)

project(${PROJECT} LANGUAGES CXX)

# Force C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer Ninja, but allow others. Add common warning flags.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Example: for each target use modern target properties
# target_compile_features(my_target PRIVATE cxx_std_17)
# target_include_directories(my_target PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sources)

message(STATUS "System: " ${CMAKE_HOST_SYSTEM_NAME} " " ${CMAKE_HOST_SYSTEM_VERSION})
message(STATUS "Processor: " ${CMAKE_HOST_SYSTEM_PROCESSOR})

# add version define
set(APP_VERSION 0.1.6)

add_compile_definitions(APP_VERSION="${APP_VERSION}")

#------- headers --------
set(HEADERS
    sources/mainwindow.h
    sources/app_enums.h
    sources/imagearea.h
    sources/datasingleton.h
    sources/autorun_utils.h
    sources/effects/effectruncallback.h
    sources/effects/abstracteffect.h
    sources/effects/negativeeffect.h
    sources/effects/grayeffect.h
    sources/effects/binarizationeffect.h
    sources/effects/customeffect.h
    sources/effects/effectwithsettings.h
    sources/effects/gammaeffect.h
    sources/effects/gaussianblureffect.h
    sources/effects/scripteffect.h
    sources/effects/scripteffectwithsettings.h
    sources/effects/sharpeneffect.h
    sources/qtsingleapplication/qtlocalpeer.h
    sources/qtsingleapplication/qtsingleapplication.h
    sources/qtsingleapplication/qtsinglecoreapplication.h
    sources/ScriptInfo.h
    sources/ScriptModel.h
    sources/set_dark_theme.h
    sources/undocommand.h
    sources/widgets/toolbar.h
    sources/widgets/colorchooser.h
    sources/widgets/palettebar.h
    sources/widgets/palettebutton.h
    sources/widgets/shortcutedit.h
    sources/widgets/abstracteffectsettings.h
    sources/widgets/customfiltersettings.h
    sources/widgets/sharpenfiltersettings.h
    sources/widgets/gaussianblurfiltersettings.h
    sources/widgets/PythonConsoleWidget.h
    sources/widgets/scripteffectsettings.h
    sources/dialogs/resizedialog.h
    sources/dialogs/settingsdialog.h
    sources/dialogs/textdialog.h
    sources/dialogs/effectsettingsdialog.h
    sources/dialogs/SpinnerOverlay.h
    sources/instruments/abstractinstrument.h
    sources/instruments/abstractselection.h
    sources/instruments/selectioninstrument.h
    sources/instruments/pencilinstrument.h
    sources/instruments/lineinstrument.h
    sources/instruments/eraserinstrument.h
    sources/instruments/rectangleinstrument.h
    sources/instruments/ellipseinstrument.h
    sources/instruments/fillinstrument.h
    sources/instruments/sprayinstrument.h
    sources/instruments/magnifierinstrument.h
    sources/instruments/colorpickerinstrument.h
    sources/instruments/curvelineinstrument.h
    sources/instruments/textinstrument.h
)

#------- sources --------
set(SOURCES
    sources/main.cpp
    sources/mainwindow.cpp
    sources/imagearea.cpp
    sources/datasingleton.cpp
    sources/autorun_utils.cpp
    sources/effects/abstracteffect.cpp
    sources/effects/customeffect.cpp
    sources/effects/negativeeffect.cpp
    sources/effects/grayeffect.cpp
    sources/effects/binarizationeffect.cpp
    sources/effects/effectwithsettings.cpp
    sources/effects/gammaeffect.cpp
    sources/effects/scripteffect.cpp
    sources/effects/scripteffectwithsettings.cpp
    sources/qtsingleapplication/qtlocalpeer.cpp
    sources/qtsingleapplication/qtsingleapplication.cpp
    sources/qtsingleapplication/qtsinglecoreapplication.cpp
    sources/ScriptModel.cpp
    sources/set_dark_theme.cpp
    sources/undocommand.cpp
    sources/widgets/toolbar.cpp
    sources/widgets/colorchooser.cpp
    sources/widgets/palettebar.cpp
    sources/widgets/palettebutton.cpp
    sources/widgets/shortcutedit.cpp
    sources/widgets/customfiltersettings.cpp
    sources/widgets/sharpenfiltersettings.cpp
    sources/widgets/gaussianblurfiltersettings.cpp
    sources/widgets/PythonConsoleWidget.cpp
    sources/widgets/scripteffectsettings.cpp
    sources/dialogs/resizedialog.cpp
    sources/dialogs/settingsdialog.cpp
    sources/dialogs/textdialog.cpp
    sources/dialogs/effectsettingsdialog.cpp
    sources/instruments/abstractinstrument.cpp
    sources/instruments/abstractselection.cpp
    sources/instruments/selectioninstrument.cpp
    sources/instruments/pencilinstrument.cpp
    sources/instruments/lineinstrument.cpp
    sources/instruments/eraserinstrument.cpp
    sources/instruments/rectangleinstrument.cpp
    sources/instruments/ellipseinstrument.cpp
    sources/instruments/fillinstrument.cpp
    sources/instruments/sprayinstrument.cpp
    sources/instruments/magnifierinstrument.cpp
    sources/instruments/colorpickerinstrument.cpp
    sources/instruments/curvelineinstrument.cpp
    sources/instruments/textinstrument.cpp
)

#------- resources -------
set(RESOURCE_PATH
    sources
)

set(RESOURCES
    ${RESOURCE_PATH}/resources.qrc
)

#------- languages -------
if(NOT NO_TRANSLATIONS)
    set(TRANSLATIONS
        sources/translations/translations_ru_RU.ts
        sources/translations/translations_cs_CZ.ts
        sources/translations/translations_zh_CN.ts
        sources/translations/translations_fr_FR.ts
    )
endif()

include_directories(./)

# --- Qt detection (Qt6 preferred, fallback to Qt5) ---
find_package(QT NAMES Qt6 Qt5 COMPONENTS
    Widgets
    Concurrent
    Network
    PrintSupport
    LinguistTools
    REQUIRED
)

find_package(Qt${QT_VERSION_MAJOR} COMPONENTS
    Widgets
    Concurrent
    Network
    PrintSupport
    LinguistTools
    REQUIRED
)

# --- Qt resource / MOC / translation handling ---
if(QT_VERSION_MAJOR EQUAL 6)

    qt_add_resources(QRC_SOURCES ${RESOURCES})

    qt_wrap_cpp(MOC_SOURCES ${HEADERS})

    # qt_add_translations(TRANSLATIONS_QM ${TRANSLATIONS})
    # qt_add_translations(${PROJECT}_translations
    #     TS_FILES ${TRANSLATIONS}
    #     QM_FILES_OUTPUT_VARIABLE TRANSLATIONS_QM
    # )

    qt6_create_translation(
        TRANSLATIONS_QM
        ${CMAKE_SOURCE_DIR}
        ${TRANSLATIONS}
    )

else()

    qt5_add_resources(QRC_SOURCES ${RESOURCES})

    qt5_wrap_cpp(MOC_SOURCES ${HEADERS})

    qt5_add_translation(
        TRANSLATIONS_QM
        ${TRANSLATIONS}
    )

endif()

add_definitions(-Wall)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SOURCES})
source_group("Generated Files" FILES ${MOC_SOURCES})
source_group("Resource Files" FILES ${QRC_SOURCES})

# --- Python & pybind11 ---
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

message(STATUS "Python executable: ${Python3_EXECUTABLE}")
message(STATUS "Python version:    ${Python3_VERSION}")
message(STATUS "Python includes:   ${Python3_INCLUDE_DIRS}")
message(STATUS "Python libraries:  ${Python3_LIBRARIES}")

find_package(pybind11 CONFIG REQUIRED)

# --- Build executable ---
add_executable(${PROJECT}
    ${HEADERS}
    ${SOURCES}
    ${MOC_SOURCES}
    ${QRC_SOURCES}
    ${TRANSLATIONS_QM}
)

# --- Platform-specific tweaks ---
if(WIN32)

    # On Windows, use the GUI subsystem (no console window)
    set_target_properties(${PROJECT} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

elseif(APPLE)

    # On macOS, mark as a bundle (no terminal window when launched from Finder)
    set_target_properties(${PROJECT} PROPERTIES
        MACOSX_BUNDLE TRUE
    )

else()

    # On Linux/Unix, there is no subsystem flag.
    # To avoid a terminal, launch from a desktop entry or with nohup/setsid.
    # Nothing special needed in CMake.

endif()

target_link_libraries(${PROJECT}
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::PrintSupport
    ${Python3_LIBRARIES}
    pybind11::pybind11
)

# --- Installation (Linux) ---
if(UNIX AND NOT APPLE)

    install(
        TARGETS luster-ko
        RUNTIME DESTINATION bin
    )

    install(
        FILES "sources/media/luster-ko.desktop"
        DESTINATION share/applications
    )

    install(
        FILES "sources/media/logo/l_64.png"
        DESTINATION share/pixmaps
    )

    install(
        FILES ${TRANSLATIONS_QM}
        DESTINATION share/luster-ko/translations
    )

endif()

# ============================================================================
# Windows installation / deployment
#
# There are deliberately TWO deployment paths:
#
# 1. vcpkg toolchain active:
#      - use vcpkg's applocal.ps1
#      - use the ACTUAL VCPKG_TARGET_TRIPLET
#      - this deploys the complete vcpkg DLL dependency tree, including
#        Python DLLs when Python itself comes from vcpkg.
#
# 2. no vcpkg toolchain:
#      - deploy Python runtime DLL explicitly from Python3_EXECUTABLE
#      - Qt deployment is still performed normally
#      - MSVC/UCRT runtime deployment is still performed normally
#
# This means the same CMakeLists.txt works both in CI without vcpkg and
# in a local build configured with a vcpkg toolchain.
# ============================================================================

if(MSVC)

    set(CMAKE_INSTALL_PREFIX
        "${CMAKE_BINARY_DIR}/bin"
        CACHE PATH "Install prefix" FORCE
    )

    set(BINARY_INSTALL_DIR .)

    install(
        TARGETS ${PROJECT_NAME}
        DESTINATION ${BINARY_INSTALL_DIR}
    )

    set(BINARY_FILE
        "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe"
    )

    # ------------------------------------------------------------------------
    # Detect whether vcpkg is ACTUALLY being used as the CMake toolchain.
    #
    # Do not merely test whether CMAKE_TOOLCHAIN_FILE exists: another
    # toolchain may be in use.
    # ------------------------------------------------------------------------

    set(_VCPKG_ACTIVE FALSE)
    set(_VCPKG_ROOT "")
    set(_VCPKG_TOOLCHAIN_DIR "")
    set(_VCPKG_APLOCAL "")
    set(_VCPKG_TRIPLET "")
    set(_VCPKG_INSTALLED_ROOT "")
    set(_VCPKG_BIN_DIR "")

    if(DEFINED CMAKE_TOOLCHAIN_FILE
       AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")

        get_filename_component(
            _TOOLCHAIN_FILE_NAME
            "${CMAKE_TOOLCHAIN_FILE}"
            NAME
        )

        if(_TOOLCHAIN_FILE_NAME STREQUAL "vcpkg.cmake")

            get_filename_component(
                _VCPKG_TOOLCHAIN_DIR
                "${CMAKE_TOOLCHAIN_FILE}"
                DIRECTORY
            )

            set(_VCPKG_APLOCAL
                "${_VCPKG_TOOLCHAIN_DIR}/msbuild/applocal.ps1"
            )

            if(EXISTS "${_VCPKG_APLOCAL}")

                set(_VCPKG_ACTIVE TRUE)

                # vcpkg.cmake is normally:
                #
                #   <vcpkg-root>/scripts/buildsystems/vcpkg.cmake
                #
                get_filename_component(
                    _VCPKG_ROOT
                    "${_VCPKG_TOOLCHAIN_DIR}/../.."
                    ABSOLUTE
                )

                # Prefer the explicitly selected triplet. This is important
                # for custom triplets such as:
                #
                #   x64-windows-md-release
                #
                if(DEFINED VCPKG_TARGET_TRIPLET
                   AND NOT "${VCPKG_TARGET_TRIPLET}" STREQUAL "")

                    set(
                        _VCPKG_TRIPLET
                        "${VCPKG_TARGET_TRIPLET}"
                    )

                elseif(DEFINED VCPKG_DEFAULT_TRIPLET
                       AND NOT "${VCPKG_DEFAULT_TRIPLET}" STREQUAL "")

                    set(
                        _VCPKG_TRIPLET
                        "${VCPKG_DEFAULT_TRIPLET}"
                    )

                elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)

                    set(
                        _VCPKG_TRIPLET
                        "x64-windows"
                    )

                else()

                    set(
                        _VCPKG_TRIPLET
                        "x86-windows"
                    )

                endif()

                # VCPKG_INSTALLED_DIR is used by manifest-mode builds.
                # Classic vcpkg uses <vcpkg-root>/installed.
                if(DEFINED VCPKG_INSTALLED_DIR
                   AND NOT "${VCPKG_INSTALLED_DIR}" STREQUAL "")

                    set(
                        _VCPKG_INSTALLED_ROOT
                        "${VCPKG_INSTALLED_DIR}"
                    )

                else()

                    set(
                        _VCPKG_INSTALLED_ROOT
                        "${_VCPKG_ROOT}/installed"
                    )

                endif()

                set(
                    _VCPKG_BIN_DIR
                    "${_VCPKG_INSTALLED_ROOT}/${_VCPKG_TRIPLET}/bin"
                )

                message(STATUS "")
                message(STATUS "=== vcpkg deployment ===")
                message(STATUS "vcpkg root:       ${_VCPKG_ROOT}")
                message(STATUS "vcpkg triplet:    ${_VCPKG_TRIPLET}")
                message(STATUS "vcpkg installed:  ${_VCPKG_INSTALLED_ROOT}")
                message(STATUS "vcpkg bin:        ${_VCPKG_BIN_DIR}")
                message(STATUS "applocal.ps1:     ${_VCPKG_APLOCAL}")
                message(STATUS "========================")
                message(STATUS "")

            endif()

        endif()

    endif()

    # ------------------------------------------------------------------------
    # vcpkg deployment
    #
    # applocal.ps1 examines the executable and copies the required runtime
    # DLLs from the selected vcpkg triplet. This is preferable to copying
    # every DLL blindly.
    #
    # In particular, if Python came from vcpkg, python311.dll (or the
    # corresponding Python version) is part of this deployment.
    # ------------------------------------------------------------------------

    if(_VCPKG_ACTIVE)

        if(NOT EXISTS "${_VCPKG_BIN_DIR}")

            message(
                FATAL_ERROR
                "vcpkg is active, but the selected installed bin directory "
                "does not exist:\n"
                "  ${_VCPKG_BIN_DIR}\n"
                "Triplet: ${_VCPKG_TRIPLET}"
            )

        endif()

        install(
            CODE
            "execute_process(
                COMMAND powershell
                        -NoProfile
                        -ExecutionPolicy Bypass
                        -File \"${_VCPKG_APLOCAL}\"
                        -targetBinary \"${BINARY_FILE}\"
                        -installedDir \"${_VCPKG_BIN_DIR}\"
                RESULT_VARIABLE _vcpkg_applocal_result
                OUTPUT_VARIABLE _vcpkg_applocal_output
                ERROR_VARIABLE _vcpkg_applocal_error
            )

            message(STATUS
                \"vcpkg applocal output:\\n\${_vcpkg_applocal_output}\"
            )

            if(NOT _vcpkg_applocal_result EQUAL 0)
                message(
                    FATAL_ERROR
                    \"vcpkg applocal.ps1 failed with exit code
                    \${_vcpkg_applocal_result}:\\n
                    \${_vcpkg_applocal_error}\"
                )
            endif()
            "
        )

    else()

        # --------------------------------------------------------------------
        # Non-vcpkg deployment
        #
        # CI uses actions/setup-python here. Python3::Python is linked from
        # that installation, so its Windows runtime DLL must accompany the
        # executable.
        #
        # Example:
        #
        #   .../Python311/python.exe
        #   .../Python311/python311.dll
        #
        # FindPython3 gives us the executable; derive the standard Windows
        # runtime DLL name from the detected Python major/minor version.
        # --------------------------------------------------------------------

        message(STATUS "")
        message(STATUS "=== non-vcpkg deployment ===")
        message(STATUS "Python executable: ${Python3_EXECUTABLE}")
        message(STATUS "Python version:    ${Python3_VERSION}")
        message(STATUS "============================")
        message(STATUS "")

        get_filename_component(
            _PYTHON_RUNTIME_DIR
            "${Python3_EXECUTABLE}"
            DIRECTORY
        )

        set(
            _PYTHON_RUNTIME_DLL
            "${_PYTHON_RUNTIME_DIR}/python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}.dll"
        )

        if(EXISTS "${_PYTHON_RUNTIME_DLL}")

            message(
                STATUS
                "Python runtime DLL: ${_PYTHON_RUNTIME_DLL}"
            )

            install(
                FILES "${_PYTHON_RUNTIME_DLL}"
                DESTINATION .
            )

        else()

            message(
                FATAL_ERROR
                "Could not find the Python runtime DLL required by the "
                "non-vcpkg build.\n"
                "Expected:\n"
                "  ${_PYTHON_RUNTIME_DLL}\n"
                "Python executable:\n"
                "  ${Python3_EXECUTABLE}\n"
                "Python version:\n"
                "  ${Python3_VERSION}"
            )

        endif()

    endif()

    # ------------------------------------------------------------------------
    # MSVC / Universal CRT deployment
    # ------------------------------------------------------------------------

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)

    include(InstallRequiredSystemLibraries)

    # ------------------------------------------------------------------------
    # Qt 6 deployment
    # ------------------------------------------------------------------------

    if(QT_VERSION_MAJOR EQUAL 6)

        if(EXISTS "${Qt6_DIR}/Qt6DeploySupport/Qt6DeploySupport.cmake")

            include(Qt6DeploySupport)

            qt_deploy_runtime_dependencies(
                EXECUTABLES
                    "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe"
            )

        else()

            message(
                WARNING
                "Qt6DeploySupport not found — skipping Qt 6 deployment"
            )

        endif()

    endif()

    # ------------------------------------------------------------------------
    # Qt 5 deployment
    # ------------------------------------------------------------------------

    if(QT_VERSION_MAJOR EQUAL 5)

        get_target_property(
            _qmake_executable
            Qt5::qmake
            IMPORTED_LOCATION
        )

        if(NOT _qmake_executable)

            message(
                FATAL_ERROR
                "Could not determine Qt5 qmake executable."
            )

        endif()

        get_filename_component(
            _qt_bin_dir
            "${_qmake_executable}"
            DIRECTORY
        )

        find_program(
            WINDEPLOYQT_EXECUTABLE
            windeployqt
            HINTS "${_qt_bin_dir}"
        )

        if(NOT WINDEPLOYQT_EXECUTABLE)

            message(
                FATAL_ERROR
                "windeployqt was not found in: ${_qt_bin_dir}"
            )

        endif()

        install(
            CODE
            "execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        \"${BINARY_FILE}\"
                WORKING_DIRECTORY \"${_qt_bin_dir}\"
                RESULT_VARIABLE _windeployqt_result
            )

            if(NOT _windeployqt_result EQUAL 0)
                message(
                    FATAL_ERROR
                    \"windeployqt failed with exit code
                    \${_windeployqt_result}\"
                )
            endif()
            "
        )

    endif()

    # ------------------------------------------------------------------------
    # Translation files
    # ------------------------------------------------------------------------

    install(
        FILES ${TRANSLATIONS_QM}
        DESTINATION translations
    )

endif()
