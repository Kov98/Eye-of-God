//! Confere o parser Rust contra o oraculo independente em Python (tools/gen_corpus.py):
//! o plistlib escreve o plist e o le de volta; o JSON esperado foi calculado a partir do
//! que o plistlib leu, nao do parser Rust.
//!
//! Esses arquivos sao gerados (nao versionados como fonte de verdade): se o diretorio nao
//! existir, o teste avisa e passa, para o crate continuar testavel sem o gerador.

use std::path::Path;

fn crate_dir() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf()
}

#[test]
fn generated_plists_match_the_python_oracle() {
    let dir = crate_dir().join("tests").join("corpus").join("gen");
    let Ok(entries) = std::fs::read_dir(&dir) else {
        println!("corpus gerado ausente em {}: rode python tools/gen_corpus.py", dir.display());
        return;
    };
    let mut names: Vec<std::path::PathBuf> = entries
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|ext| ext == "plist"))
        .collect();
    names.sort();
    assert!(!names.is_empty(), "nenhum plist gerado em {}", dir.display());

    let mut checked = 0;
    for path in names {
        let blob = std::fs::read(&path).expect("ler plist gerado");
        let expected_path = path.with_extension("json");
        let expected = std::fs::read_to_string(&expected_path).expect("ler json esperado");
        let value = formatguard::plist::parse(&blob)
            .unwrap_or_else(|error| panic!("{} recusado: {error}", path.display()));
        let json = formatguard::json::plist_to_json(&value).expect("serializar");
        assert_eq!(
            String::from_utf8_lossy(&json),
            expected,
            "divergencia Rust x plistlib em {}",
            path.display()
        );
        checked += 1;
    }
    println!("plists conferidos contra o plistlib: {checked}");
    assert!(checked >= 9, "esperado o corpus completo, conferi {checked}");
}

#[test]
fn generated_protobuf_vectors_match_the_python_encoder() {
    let dir = crate_dir().join("tests").join("corpus").join("proto");
    let Ok(entries) = std::fs::read_dir(&dir) else {
        println!("corpus gerado ausente em {}: rode python tools/gen_corpus.py", dir.display());
        return;
    };
    let mut names: Vec<std::path::PathBuf> = entries
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|ext| ext == "bin"))
        .collect();
    names.sort();
    assert!(!names.is_empty(), "nenhum vetor protobuf gerado em {}", dir.display());

    let mut checked = 0;
    for path in names {
        let blob = std::fs::read(&path).expect("ler vetor protobuf");
        let expected_path = path.with_extension("json");
        let expected = std::fs::read_to_string(&expected_path).expect("ler json esperado");
        let message = formatguard::protobuf::parse(&blob)
            .unwrap_or_else(|error| panic!("{} recusado: {error}", path.display()));
        let json = formatguard::json::protobuf_to_json(&message).expect("serializar");
        assert_eq!(
            String::from_utf8_lossy(&json),
            expected,
            "divergencia Rust x codificador Python em {}",
            path.display()
        );
        checked += 1;
    }
    println!("vetores protobuf conferidos: {checked}");
    assert!(checked >= 10, "esperado o corpus completo, conferi {checked}");
}
