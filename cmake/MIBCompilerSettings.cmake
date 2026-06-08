# Suppress character encoding warnings from third-party headers.
if(MSVC)
    add_compile_options(/wd4828)
endif()

# Preserve PDBs and address-to-source mapping for Release builds so that
# minidumps from end-user crashes can be symbolicated. /Zi generates the
# .pdb; /DEBUG instructs the linker to emit and keep it; /OPT:REF /OPT:ICF
# restore Release optimizations that /DEBUG would otherwise disable.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
endif()
