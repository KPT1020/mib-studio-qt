if(NOT TARGET mib_studio_qt)
    message(FATAL_ERROR "cmake/packaging.cmake must be included after mib_studio_qt is defined")
endif()

if(WIN32 AND MIB_ENABLE_WINDOWS_PACKAGING)
    find_program(WINDEPLOYQT_EXE windeployqt.exe)
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET mib_studio_qt POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" "$<TARGET_FILE:mib_studio_qt>"
            COMMENT "Deploying Qt runtime for mib_studio_qt"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found - Qt deployment will be skipped")
    endif()

    add_custom_command(TARGET mib_studio_qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/include/Coremor/XMT_DLL_SER.dll"
                "$<TARGET_FILE_DIR:mib_studio_qt>/"
        COMMENT "Copy Coremor DLL"
        VERBATIM)

    add_custom_command(TARGET mib_studio_qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:mib_studio_qt>
                $<TARGET_FILE_DIR:mib_studio_qt>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs for mib_studio_qt")

    add_custom_command(TARGET mib_studio_qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:mib_studio_qt>/resources/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/resources/models/yolo11n-seg.onnx"
                "$<TARGET_FILE_DIR:mib_studio_qt>/resources/models/"
        COMMENT "Copying model resources"
        VERBATIM)

    if(MIB_SENTRY_AVAILABLE AND TARGET crashpad_handler)
        add_custom_command(TARGET mib_studio_qt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:crashpad_handler>"
                    "$<TARGET_FILE_DIR:mib_studio_qt>/"
            COMMENT "Copying crashpad_handler.exe"
            VERBATIM)
    endif()

    find_program(ISCC_EXE
        NAMES ISCC.exe
        PATHS
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
        DOC "InnoSetup Compiler")

    if(ISCC_EXE)
        set(INSTALLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dist")
        file(MAKE_DIRECTORY "${INSTALLER_OUTPUT_DIR}")

        set(MIB_SENTRY_DSN "" CACHE STRING
            "Sentry DSN baked into the installer (HKLM env var)")
        set(MIB_SENTRY_ENVIRONMENT "production" CACHE STRING
            "Sentry environment tag baked into the installer")

        set(_ISCC_SENTRY_FLAGS)
        if(MIB_SENTRY_DSN)
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryDSN=${MIB_SENTRY_DSN}")
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryEnvironment=${MIB_SENTRY_ENVIRONMENT}")
        endif()

        add_custom_target(package_installer
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS}
                    "/O${INSTALLER_OUTPUT_DIR}"
                    "${PROJECT_SOURCE_DIR}/resources/installers/mib-studio-qt.iss"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Building Windows installer with InnoSetup"
            DEPENDS mib_studio_qt)

        add_custom_target(package_installer_update
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS}
                    "/O${INSTALLER_OUTPUT_DIR}"
                    "${PROJECT_SOURCE_DIR}/resources/installers/mib-studio-qt-update.iss"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Building Windows update package with InnoSetup"
            DEPENDS mib_studio_qt)
    else()
        message(STATUS "InnoSetup not found - installer targets will not be available")
    endif()
endif()
