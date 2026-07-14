set(MIB_ENABLE_HARDWARE_SDKS_DEFAULT OFF)
if(WIN32)
    set(MIB_ENABLE_HARDWARE_SDKS_DEFAULT ON)
endif()
option(MIB_ENABLE_HARDWARE_SDKS
    "Enable proprietary Windows hardware SDK integrations (EGrabber/Coremor)"
    ${MIB_ENABLE_HARDWARE_SDKS_DEFAULT})

set(MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT OFF)
if(WIN32)
    set(MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT ON)
endif()
option(MIB_ENABLE_WINDOWS_PACKAGING
    "Enable Windows runtime deployment and InnoSetup packaging targets"
    ${MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT})

set(MIB_ENABLE_MINDVISION_DEFAULT OFF)
option(MIB_ENABLE_MINDVISION
    "Enable MindVision camera SDK integration (Windows only, requires external SDK)"
    ${MIB_ENABLE_MINDVISION_DEFAULT})

option(MIB_BUILD_BACKEND_ONLY
    "Build only backend targets (no frontend executables)"
    OFF)

option(MIB_BUILD_PYTHON_BINDINGS
    "Build the pybind11 Python bindings for mib_processing (bindings/python/)"
    OFF)

set(MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256 "" CACHE STRING
    "Approved Authenticode signer SubjectPublicKeyInfo SHA-256 for native processing cores")
option(MIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI
    "Require a non-empty approved processing-core signer SPKI SHA-256"
    OFF)

# Development and fork CI builds may intentionally leave the signer pin empty,
# but any supplied value must still be unambiguous. Official release entry
# points opt into the non-empty requirement before producing an installer.
string(STRIP "${MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256}"
    _mib_processing_core_signer_spki_sha256)
if(_mib_processing_core_signer_spki_sha256)
    string(LENGTH "${_mib_processing_core_signer_spki_sha256}"
        _mib_processing_core_signer_spki_sha256_length)
    if(NOT _mib_processing_core_signer_spki_sha256_length EQUAL 64 OR
       _mib_processing_core_signer_spki_sha256 MATCHES "[^0-9A-Fa-f]")
        message(FATAL_ERROR
            "MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256 must be exactly 64 hexadecimal characters")
    endif()
    string(TOLOWER "${_mib_processing_core_signer_spki_sha256}"
        _mib_processing_core_signer_spki_sha256)
elseif(MIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI)
    message(FATAL_ERROR
        "A production build requires MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256")
endif()
set(MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256
    "${_mib_processing_core_signer_spki_sha256}" CACHE STRING
    "Approved Authenticode signer SubjectPublicKeyInfo SHA-256 for native processing cores"
    FORCE)
unset(_mib_processing_core_signer_spki_sha256)
unset(_mib_processing_core_signer_spki_sha256_length)
