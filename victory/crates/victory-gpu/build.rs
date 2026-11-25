fn main() {
    let target = std::env::var("TARGET").unwrap_or_default();
    if target.contains("windows") {
        cc::Build::new()
            .file("src/seh.c")
            .compile("victory_gpu_seh");
        println!("cargo:rerun-if-changed=src/seh.c");
    }
}
