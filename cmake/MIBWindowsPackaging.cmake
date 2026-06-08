function(mib_add_windows_installer_targets app_target)
    if(NOT WIN32 OR NOT MIB_ENABLE_WINDOWS_PACKAGING)
        return()
    endif()

    if(NOT TARGET ${app_target})
        message(WARNING "Installer targets skipped because ${app_target} is not defined")
        return()
    endif()

    # Find InnoSetup compiler.
    find_program(ISCC_EXE
        NAMES ISCC.exe
        PATHS
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
        DOC "InnoSetup Compiler"
    )

    if(ISCC_EXE)
        # Create dist directory.
        set(INSTALLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dist")
        file(MAKE_DIRECTORY "${INSTALLER_OUTPUT_DIR}")

        # InnoSetup script paths.
        set(INNOSETUP_SCRIPT "${PROJECT_SOURCE_DIR}/resources/installers/mib-studio-qt.iss")
        set(INNOSETUP_UPDATE_SCRIPT "${PROJECT_SOURCE_DIR}/resources/installers/mib-studio-qt-update.iss")

        # Sentry DSN / environment passed through to InnoSetup so installed
        # builds get MIB_SENTRY_DSN / MIB_CRASH_ENV as system env vars. Empty
        # by default - release CI passes the real value via -D on configure.
        set(MIB_SENTRY_DSN "" CACHE STRING
            "Sentry DSN baked into the installer (HKLM env var)")
        set(MIB_SENTRY_ENVIRONMENT "production" CACHE STRING
            "Sentry environment tag baked into the installer")

        set(_ISCC_SENTRY_FLAGS)
        if(MIB_SENTRY_DSN)
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryDSN=${MIB_SENTRY_DSN}")
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryEnvironment=${MIB_SENTRY_ENVIRONMENT}")
        endif()

        # Custom target to build full installer.
        add_custom_target(package_installer
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows installer..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist.
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            # Build installer.
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup script..."
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS} "/O${INSTALLER_OUTPUT_DIR}" "${INNOSETUP_SCRIPT}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Building Windows installer with InnoSetup"
            DEPENDS ${app_target}
        )

        # Custom target to build update package (app files only, no eGrabber/VC++).
        add_custom_target(package_installer_update
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows update package..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist.
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            # Build update package.
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup update script..."
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS} "/O${INSTALLER_OUTPUT_DIR}" "${INNOSETUP_UPDATE_SCRIPT}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Building Windows update package with InnoSetup"
            DEPENDS ${app_target}
        )

        message(STATUS "InnoSetup found: ${ISCC_EXE}")
        message(STATUS "Installer target 'package_installer' available (full installer)")
        message(STATUS "Installer target 'package_installer_update' available (update package)")
        message(STATUS "Run 'cmake --build build --target package_installer' to create full installer")
        message(STATUS "Run 'cmake --build build --target package_installer_update' to create update package")
    else()
        message(STATUS "InnoSetup not found - installer target will not be available")
        message(STATUS "Install InnoSetup from https://jrsoftware.org/isdl.php to enable installer generation")
    endif()
endfunction()
