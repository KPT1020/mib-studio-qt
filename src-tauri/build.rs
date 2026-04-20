use std::collections::BTreeSet;
use std::path::PathBuf;

fn main() {
    tauri_build::build();

    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let root = manifest_dir.parent().expect("src-tauri has a parent").to_path_buf();
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
    let mut link_libs: Vec<String> = Vec::new();
    let mut system_libs: BTreeSet<String> = BTreeSet::new();
    let mut seen_libs: BTreeSet<String> = BTreeSet::new();

    let data_files = std::fs::read_dir(&build_dir)
        .expect("build/ missing — run cmake --preset windows-default first");
    for entry in data_files.flatten() {
        let path = entry.path();
        let Some(name) = path.file_name().and_then(|s| s.to_str()) else { continue; };
        if !name.ends_with("-release-x86_64-data.cmake") { continue; }
        if name.starts_with("module-") { continue; }

        let Ok(contents) = std::fs::read_to_string(&path) else { continue; };

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
                    } else if var.ends_with("_LIBS_RELEASE") && !var.contains("SYSTEM_LIBS") {
                        for lib in value.split_whitespace() {
                            let lib = lib.trim();
                            if lib.is_empty() { continue; }
                            if seen_libs.insert(lib.to_string()) {
                                link_libs.push(lib.to_string());
                            }
                        }
                    } else if var.ends_with("_SYSTEM_LIBS_RELEASE") {
                        for lib in value.split_whitespace() {
                            let lib = lib.trim();
                            if lib.is_empty() { continue; }
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
        println!("cargo:rustc-link-lib={}", lib);
    }
    for lib in &system_libs {
        println!("cargo:rustc-link-lib={}", lib);
    }

    // Windows system libs that Qt/OpenSSL/etc commonly need even if not listed.
    for lib in ["ws2_32", "crypt32", "secur32", "userenv", "iphlpapi", "bcrypt", "ncrypt"] {
        println!("cargo:rustc-link-lib={}", lib);
    }

    println!("cargo:rerun-if-changed=src/bridge/ffi.rs");
    println!("cargo:rerun-if-changed=src/bridge/bridge.cpp");
    println!("cargo:rerun-if-changed=src/bridge/bridge.h");
    println!("cargo:rerun-if-changed={}", mib_backend_lib.display());
}

fn strip_prefix_ci<'a>(s: &'a str, prefix: &str) -> Option<&'a str> {
    if s.len() >= prefix.len() && s[..prefix.len()].eq_ignore_ascii_case(prefix) {
        Some(&s[prefix.len()..])
    } else {
        None
    }
}

// Parse `VAR "value"` or `VAR value1 value2 ...)` from a set(...) body.
// Input already has the leading `set(` stripped; trailing `)` may be present.
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
