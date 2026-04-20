#[cfg(windows)]
use std::collections::BTreeSet;
#[cfg(windows)]
use std::path::{Path, PathBuf};

#[cfg(not(windows))]
fn main() {
    // Tauri desktop runtime dependencies are platform-specific and not
    // available in the default Linux cloud image; skip native bridge build.
    println!("cargo:warning=Non-Windows build: skipping native mib bridge compilation");
}

#[cfg(windows)]
fn main() {
    tauri_build::build();

    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let root = manifest_dir
        .parent()
        .expect("src-tauri has a parent")
        .to_path_buf();
    let build_dir = root.join("build");
    let lib_dir = build_dir.join("Release");
    let include_dir = root.join("include");
    let src_dir = root.join("src");

    let mib_backend_lib = lib_dir.join("mib_backend.lib");
    assert!(
        mib_backend_lib.exists(),
        "mib_backend.lib not found at {} — build it first: `cmake --build build --preset windows-default-build-release --target mib_backend`",
        mib_backend_lib.display()
    );

    // Harvest Conan package include dirs (OpenCV, etc.) + link dirs/libs from *-release-x86_64-data.cmake.
    let mut cxx_includes: BTreeSet<PathBuf> = BTreeSet::new();
    let mut link_search: BTreeSet<PathBuf> = BTreeSet::new();
    let mut package_bins: BTreeSet<PathBuf> = BTreeSet::new();
    let mut link_libs: Vec<String> = Vec::new();
    let mut system_libs: BTreeSet<String> = BTreeSet::new();
    let mut seen_libs: BTreeSet<String> = BTreeSet::new();

    let data_files = std::fs::read_dir(&build_dir)
        .expect("build/ missing — run cmake --preset windows-default first");
    for entry in data_files.flatten() {
        let path = entry.path();
        let Some(name) = path.file_name().and_then(|s| s.to_str()) else {
            continue;
        };
        if !name.ends_with("-release-x86_64-data.cmake") {
            continue;
        }
        if name.starts_with("module-") {
            continue;
        }
        // mib_backend is Qt-free; do not link Qt from Conan (would load Qt6 DLLs at runtime).
        let name_lower = name.to_ascii_lowercase();
        if name_lower.starts_with("qt6") || name_lower.starts_with("qt-") {
            continue;
        }

        let Ok(contents) = std::fs::read_to_string(&path) else {
            continue;
        };

        for line in contents.lines() {
            let line = line.trim();
            if let Some(rest) = strip_prefix_ci(line, "set(") {
                if let Some((var, value)) = split_set(rest) {
                    if var.ends_with("_PACKAGE_FOLDER_RELEASE") {
                        let pkg = PathBuf::from(&value);
                        let inc = pkg.join("include");
                        if inc.exists() {
                            cxx_includes.insert(inc);
                        }
                        link_search.insert(pkg.join("lib"));
                        let bin_dir = pkg.join("bin");
                        if bin_dir.is_dir() {
                            package_bins.insert(bin_dir);
                        }
                    } else if var.ends_with("_LIBS_RELEASE") && !var.contains("SYSTEM_LIBS") {
                        for lib in value.split_whitespace() {
                            let lib = lib.trim();
                            if lib.is_empty() {
                                continue;
                            }
                            if seen_libs.insert(lib.to_string()) {
                                link_libs.push(lib.to_string());
                            }
                        }
                    } else if var.ends_with("_SYSTEM_LIBS_RELEASE") {
                        for lib in value.split_whitespace() {
                            let lib = lib.trim();
                            if lib.is_empty() {
                                continue;
                            }
                            system_libs.insert(lib.to_string());
                        }
                    }
                }
            }
        }
    }

    let mut cxx = cxx_build::bridge("src/bridge/ffi.rs");
    cxx.file("src/bridge/bridge.cpp")
        .include(manifest_dir.join("src"))
        .include(&include_dir)
        .include(&src_dir);
    for inc in &cxx_includes {
        cxx.include(inc);
    }
    cxx.std("c++17").compile("mib_bridge");

    // mib_backend.lib + any generated vcpkg/Conan libs Conan copied into build/Release.
    println!("cargo:rustc-link-search=native={}", lib_dir.display());
    println!("cargo:rustc-link-lib=static=mib_backend");

    // Coremor nanopositioner SDK — checked into repo, not via Conan.
    let coremor_dir = root.join("include").join("Coremor");
    println!("cargo:rustc-link-search=native={}", coremor_dir.display());
    println!("cargo:rustc-link-lib=XMT_DLL_SER");

    for p in &link_search {
        if p.exists() {
            println!("cargo:rustc-link-search=native={}", p.display());
        }
    }
    for lib in &link_libs {
        if lib.to_ascii_lowercase().starts_with("qt6") {
            panic!(
                "Qt6 library leaked into Tauri link line: {} — check Conan data file filtering",
                lib
            );
        }
        println!("cargo:rustc-link-lib={}", lib);
    }
    for lib in &system_libs {
        println!("cargo:rustc-link-lib={}", lib);
    }

    // Windows system libs that Qt/OpenSSL/etc commonly need even if not listed.
    for lib in [
        "ws2_32", "crypt32", "secur32", "userenv", "iphlpapi", "bcrypt", "ncrypt",
    ] {
        println!("cargo:rustc-link-lib={}", lib);
    }

    println!("cargo:rerun-if-changed=src/bridge/ffi.rs");
    println!("cargo:rerun-if-changed=src/bridge/bridge.cpp");
    println!("cargo:rerun-if-changed=src/bridge/bridge.h");
    println!("cargo:rerun-if-changed={}", mib_backend_lib.display());

    #[cfg(windows)]
    copy_windows_runtime_dlls(&manifest_dir, &root, &package_bins);
}

/// Copy Conan `bin\\*.dll` and Coremor `XMT_DLL_SER.dll` next to `mib-studio.exe` so `cargo run` works
/// without manually setting `PATH` (avoids `0xC0000135` STATUS_DLL_NOT_FOUND).
#[cfg(windows)]
fn copy_windows_runtime_dlls(
    manifest_dir: &Path,
    repo_root: &Path,
    package_bins: &BTreeSet<PathBuf>,
) {
    let profile = match std::env::var("PROFILE") {
        Ok(p) => p,
        Err(_) => return,
    };
    let target_root = std::env::var("CARGO_TARGET_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest_dir.join("target"));
    let dest = target_root.join(&profile);
    if let Err(e) = std::fs::create_dir_all(&dest) {
        println!(
            "cargo:warning=Could not create {} for runtime DLL copy: {}",
            dest.display(),
            e
        );
        return;
    }

    let mut copied: usize = 0;
    let coremor_dll = repo_root
        .join("include")
        .join("Coremor")
        .join("XMT_DLL_SER.dll");
    if coremor_dll.is_file() {
        let out = dest.join("XMT_DLL_SER.dll");
        if std::fs::copy(&coremor_dll, &out).is_ok() {
            copied += 1;
        }
    }

    for bin_dir in package_bins {
        let Ok(entries) = std::fs::read_dir(bin_dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path
                .extension()
                .and_then(|e| e.to_str())
                .map_or(false, |e| e.eq_ignore_ascii_case("dll"))
            {
                if let Some(name) = path.file_name() {
                    let out = dest.join(name);
                    if std::fs::copy(&path, &out).is_ok() {
                        copied += 1;
                    }
                }
            }
        }
    }

    if copied > 0 {
        println!(
            "cargo:warning=Copied {} runtime DLL(s) to {} (for `cargo run` without Conan PATH)",
            copied,
            dest.display()
        );
    }
}

#[cfg(windows)]
fn strip_prefix_ci<'a>(s: &'a str, prefix: &str) -> Option<&'a str> {
    if s.len() >= prefix.len() && s[..prefix.len()].eq_ignore_ascii_case(prefix) {
        Some(&s[prefix.len()..])
    } else {
        None
    }
}

// Parse `VAR "value"` or `VAR value1 value2 ...)` from a set(...) body.
// Input already has the leading `set(` stripped; trailing `)` may be present.
#[cfg(windows)]
fn split_set(body: &str) -> Option<(&str, String)> {
    let body = body.trim_end_matches(')').trim();
    let (var, rest) = body.split_once(char::is_whitespace)?;
    let rest = rest.trim();
    let value = if let Some(stripped) = rest.strip_prefix('"') {
        stripped.strip_suffix('"').unwrap_or(stripped).to_string()
    } else {
        rest.to_string()
    };
    Some((var, value))
}
