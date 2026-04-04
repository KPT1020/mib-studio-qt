fn main() {
    tauri_build::build();

    // TODO: Add CMake integration to build mib_backend static library
    // and the C++ bridge layer, then link them into this Rust binary.
    //
    // Example with cmake crate:
    // let dst = cmake::Config::new("..").build();
    // println!("cargo:rustc-link-search=native={}/lib", dst.display());
    // println!("cargo:rustc-link-lib=static=mib_backend");
    // println!("cargo:rustc-link-lib=static=mib_bridge");
    //
    // For cxx bridge compilation:
    // cxx_build::bridge("src/bridge.rs")
    //     .file("../src/bridge/bridge.cpp")
    //     .include("../include")
    //     .flag_if_supported("-std=c++17")
    //     .compile("mib_bridge");
}
