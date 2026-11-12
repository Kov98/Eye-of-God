//! Testes do parser de binary plist: tipos, limites e recusas.

mod common;

use formatguard::error::FgStatus;
use formatguard::plist::{parse, PlistValue};

fn status_of(blob: &[u8]) -> FgStatus {
    match parse(blob) {
        Ok(_) => FgStatus::Ok,
        Err(error) => error.status(),
    }
}

#[test]
fn full_dictionary_matches_expected_json() {
    let objects = vec![
        common::ascii("Version"),
        common::integer(3),
        common::ascii("IsEncrypted"),
        common::boolean(false),
        common::ascii("BackupKeyBag"),
        common::data(&[0x01, 0x02, 0xFF]),
        common::ascii("Date"),
        common::date(704_444_444.5),
        common::ascii("Uid"),
        common::uid(42, 1),
        common::ascii("Items"),
        common::array(&[12, 13], 1),
        common::ascii("a"),
        common::null(),
        common::ascii("Set"),
        common::set_of(&[12], 1),
        common::ascii("$weird"),
        common::real64(1.5),
    ];
    // dict precisa das referencias; ref_size 1 cabe em 18 objetos.
    let mut with_dict = objects.clone();
    with_dict.push(common::dict(
        &[(0, 1), (2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (14, 15), (16, 17)],
        1,
    ));
    let json = common::json_of(&with_dict, 18);
    assert_eq!(
        json,
        concat!(
            r#"{"Version":3,"IsEncrypted":false,"#,
            r#""BackupKeyBag":{"$data":"0102ff"},"#,
            r#""Date":{"$date":704444444.5},"#,
            r#""Uid":{"$uid":42},"#,
            r#""Items":["a",null],"#,
            r#""Set":{"$set":["a"]},"#,
            r#""\u0024weird":1.5}"#
        )
    );
}

#[test]
fn integer_widths_and_signs() {
    assert_eq!(common::json_of(&[common::uint(0xFF, 1)], 0), "255");
    assert_eq!(common::json_of(&[common::uint(0xFFFF, 2)], 0), "65535");
    assert_eq!(common::json_of(&[common::uint(0xFFFF_FFFF, 4)], 0), "4294967295");
    assert_eq!(common::json_of(&[common::integer(-1)], 0), "-1");
    assert_eq!(common::json_of(&[common::integer(i64::MIN)], 0), "-9223372036854775808");
    assert_eq!(common::json_of(&[common::integer(i64::MAX)], 0), "9223372036854775807");
    // inteiro de 16 bytes nao cabe em i64: recusado, nao truncado.
    let mut huge = vec![0x14];
    huge.extend_from_slice(&[0_u8; 16]);
    assert_eq!(status_of(&common::encode(&[huge], 0)), FgStatus::Unsupported);
}

#[test]
fn real_widths() {
    assert_eq!(common::json_of(&[common::real64(1.5)], 0), "1.5");
    assert_eq!(common::json_of(&[common::real64(-0.0)], 0), "-0");
    assert_eq!(common::json_of(&[common::real32(0.5)], 0), "0.5");
    assert_eq!(common::json_of(&[common::real64(f64::NAN)], 0), r#""nan""#);
    assert_eq!(common::json_of(&[common::real64(f64::INFINITY)], 0), r#""inf""#);
    assert_eq!(
        common::json_of(&[common::real64(f64::NEG_INFINITY)], 0),
        r#""-inf""#
    );
    // 0.1 nao e representavel: a expansao exata tem 55 digitos, nunca arredondada.
    assert_eq!(
        formatguard::json::exact_real(0.1),
        "0.1000000000000000055511151231257827021181583404541015625"
    );
    assert_eq!(formatguard::json::exact_real(1e300).len(), 301);
    let mut wide = vec![0x24];
    wide.extend_from_slice(&[0_u8; 16]);
    assert_eq!(status_of(&common::encode(&[wide], 0)), FgStatus::Unsupported);
}

#[test]
fn date_and_bad_date_marker() {
    assert_eq!(common::json_of(&[common::date(0.0)], 0), r#"{"$date":0}"#);
    assert_eq!(common::json_of(&[common::date(-1.5)], 0), r#"{"$date":-1.5}"#);
    let mut bad = vec![0x32];
    bad.extend_from_slice(&[0_u8; 8]);
    assert_eq!(status_of(&common::encode(&[bad], 0)), FgStatus::BadMarker);
}

#[test]
fn strings_short_and_extended_lengths() {
    assert_eq!(common::json_of(&[common::ascii("")], 0), r#""""#);
    assert_eq!(common::json_of(&[common::ascii("abc")], 0), r#""abc""#);
    // 14 bytes: comprimento embutido no marcador; 15 bytes: objeto inteiro depois.
    let short = "a".repeat(14);
    let long = "b".repeat(15);
    assert_eq!(common::json_of(&[common::ascii(&short)], 0), format!("\"{short}\""));
    assert_eq!(common::json_of(&[common::ascii(&long)], 0), format!("\"{long}\""));
    let huge = "c".repeat(300);
    assert_eq!(common::json_of(&[common::ascii(&huge)], 0), format!("\"{huge}\""));
    // bytes que nao sao UTF-8 nao sao perdidos: cada byte >= 0x7F vira escape unicode.
    assert_eq!(
        common::json_of(&[common::ascii_bytes(&[0x41, 0x80, 0xFF])], 0),
        r#""A\u0080\u00ff""#
    );
    assert_eq!(
        common::json_of(&[common::ascii_bytes(b"quote \" backslash \\ tab \t")], 0),
        r#""quote \" backslash \\ tab \t""#
    );
}

#[test]
fn utf16_unicode_surrogate_pair_and_lone_surrogate() {
    assert_eq!(common::json_of(&[common::utf16("acao")], 0), r#""acao""#);
    assert_eq!(common::json_of(&[common::utf16("ação")], 0), r#""ação""#);
    assert_eq!(common::json_of(&[common::utf16("漢字")], 0), r#""漢字""#);
    // par substituto valido (emoji)
    assert_eq!(common::json_of(&[common::utf16("😀")], 0), r#""😀""#);
    // surrogate solto: vira U+FFFD em UTF-8 (mesmo resultado do WideCharToMultiByte)
    let lone = vec![0x61, 0x00]; // 1 unidade, u16 = 0xD800
    let mut bad_units = vec![0x61, 0xD8];
    bad_units.clear();
    bad_units.extend_from_slice(&[0x61, 0x00]);
    let _ = (lone, bad_units);
    let mut object = vec![0x61];
    object.extend_from_slice(&0xD800_u16.to_be_bytes());
    assert_eq!(common::json_of(&[object], 0), "\"\u{fffd}\"");
}

#[test]
fn data_sizes() {
    assert_eq!(common::json_of(&[common::data(&[])], 0), r#"{"$data":""}"#);
    assert_eq!(common::json_of(&[common::data(&[0xDE, 0xAD])], 0), r#"{"$data":"dead"}"#);
    let long: Vec<u8> = (0..=255_u8).cycle().take(1000).collect();
    let json = common::json_of(&[common::data(&long)], 0);
    assert!(json.starts_with(r#"{"$data":"00"#));
    // 10 bytes de {"$data":" + 2000 de hex + 2 de "}
    assert_eq!(json.len(), 10 + 2000 + 2);
}

#[test]
fn uid_widths() {
    assert_eq!(common::json_of(&[common::uid(0, 1)], 0), r#"{"$uid":0}"#);
    assert_eq!(common::json_of(&[common::uid(0x0123, 2)], 0), r#"{"$uid":291}"#);
    assert_eq!(common::json_of(&[common::uid(u64::MAX, 8)], 0), r#"{"$uid":18446744073709551615}"#);
    let mut too_wide = vec![0x8F];
    too_wide.extend_from_slice(&[0_u8; 16]);
    assert_eq!(status_of(&common::encode(&[too_wide], 0)), FgStatus::Unsupported);
}

#[test]
fn non_string_dict_keys() {
    let objects = vec![
        common::integer(7),            // 0: chave inteira
        common::ascii("sete"),         // 1
        common::data(&[0xAB]),         // 2: chave data
        common::ascii("dado"),         // 3
        common::array(&[3], 1),        // 4: chave array
        common::ascii("x"),            // 5
        common::dict(&[(0, 1), (2, 3), (4, 5)], 1), // 6
    ];
    assert_eq!(
        common::json_of(&objects, 6),
        r#"{"<chave integer>":"sete","ab":"dado","<chave array>":"x"}"#
    );
}

#[test]
fn nested_arrays_and_sets() {
    let objects = vec![
        common::array(&[1, 2], 1),   // 0
        common::array(&[3], 1),      // 1
        common::set_of(&[3], 1),     // 2
        common::uint(1, 1),          // 3
        common::array(&[], 1),       // 4
    ];
    assert_eq!(common::json_of(&objects, 0), r#"[[1],{"$set":[1]}]"#);
    assert_eq!(common::json_of(&objects, 4), "[]");
}

#[test]
fn trailer_is_validated() {
    let good = common::encode(&[common::null()], 0);
    assert_eq!(status_of(&good), FgStatus::Ok);
    assert_eq!(status_of(&[]), FgStatus::Truncated);
    assert_eq!(status_of(b"bplist0"), FgStatus::Truncated);
    // abaixo de 40 bytes o motivo e o tamanho, nao a assinatura (igual ao C++)
    assert_eq!(status_of(b"<?xml version=\"1.0\"?><plist/>"), FgStatus::Truncated);
    let mut xml = b"<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"></plist>".to_vec();
    xml.resize(120, b' ');
    assert_eq!(status_of(&xml), FgStatus::BadSignature);
    assert_eq!(status_of(&[0x55_u8; 200]), FgStatus::BadSignature);
    let mut wrong_version = b"bplist01".to_vec();
    wrong_version.resize(120, b' ');
    assert_eq!(status_of(&wrong_version), FgStatus::BadSignature);

    // offset_size invalido
    let mut broken = good.clone();
    let trailer = broken.len() - 32;
    broken[trailer + 6] = 0;
    assert_eq!(status_of(&broken), FgStatus::BadTrailer);
    broken[trailer + 6] = 9;
    assert_eq!(status_of(&broken), FgStatus::BadTrailer);
    broken[trailer + 6] = 1;
    broken[trailer + 7] = 0;
    assert_eq!(status_of(&broken), FgStatus::BadTrailer);

    // contagem de objetos zero
    let mut broken = good.clone();
    let trailer = broken.len() - 32;
    broken[trailer + 8..trailer + 16].copy_from_slice(&0_u64.to_be_bytes());
    assert_eq!(status_of(&broken), FgStatus::BadTrailer);

    // contagem absurda
    let mut broken = good.clone();
    broken[trailer + 8..trailer + 16].copy_from_slice(&u64::MAX.to_be_bytes());
    assert_eq!(status_of(&broken), FgStatus::BudgetExceeded);

    // raiz fora da tabela
    let mut broken = good.clone();
    broken[trailer + 16..trailer + 24].copy_from_slice(&5_u64.to_be_bytes());
    assert_eq!(status_of(&broken), FgStatus::BadTrailer);

    // tabela de offsets depois do trailer
    let mut broken = good.clone();
    broken[trailer + 24..trailer + 32].copy_from_slice(&(trailer as u64).to_be_bytes());
    assert_eq!(status_of(&broken), FgStatus::BadOffsetTable);

    // tabela que passa do fim do arquivo
    let mut broken = good.clone();
    let total = broken.len() as u64;
    broken[trailer + 24..trailer + 32].copy_from_slice(&total.to_be_bytes());
    assert_eq!(status_of(&broken), FgStatus::BadOffsetTable);
}

#[test]
fn object_offsets_and_markers_are_validated() {
    // offset apontando para fora do blob
    let mut objects = vec![common::null()];
    let mut blob = common::encode(&objects, 0);
    let offset_table = blob.len() - 32 - 1;
    blob[offset_table] = 0xF0;
    assert_eq!(status_of(&blob), FgStatus::BadReference);

    // marcadores invalidos
    objects = vec![vec![0x70]];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::Unsupported);
    objects = vec![vec![0x01]];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::BadMarker);
    objects = vec![vec![0x0A]];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::BadMarker);
    objects = vec![vec![0xE0]];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::Unsupported);
    objects = vec![vec![0xF0]];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::Unsupported);
    // 0x0B tambem e array (o C++ aceita 0xA, 0xB e 0xC)
    objects = vec![common::array(&[], 1)];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::Ok);

    // referencia fora da tabela
    let objects = vec![common::array(&[9], 1)];
    assert_eq!(status_of(&common::encode(&objects, 0)), FgStatus::BadReference);

    // Marcador no ultimo byte do arquivo pedindo comprimento estendido: nao ha byte
    // depois, entao o parser tem que devolver Truncated (nao ler o trailer como se
    // fosse corpo do objeto).
    //
    // O ultimo byte do arquivo tambem e o ultimo byte do campo offset_table do
    // trailer; por isso a tabela fica em 79 = 0x4F, coerente com o 0x4F gravado la.
    let mut blob = vec![0_u8; 132];
    blob[..8].copy_from_slice(b"bplist00");
    blob[79] = 131; // tabela de offsets: uma entrada, apontando para o ultimo byte
    blob[131] = 0x4F; // data com comprimento estendido, sem bytes depois
    let trailer = blob.len() - 32;
    blob[trailer + 6] = 1; // offset_size
    blob[trailer + 7] = 1; // ref_size
    blob[trailer + 8..trailer + 16].copy_from_slice(&1_u64.to_be_bytes());
    blob[trailer + 16..trailer + 24].copy_from_slice(&0_u64.to_be_bytes());
    blob[trailer + 24..trailer + 32].copy_from_slice(&79_u64.to_be_bytes());
    assert_eq!(status_of(&blob), FgStatus::Truncated);
}

#[test]
fn lengths_are_never_trusted() {
    // data com comprimento maior que o buffer restante
    let mut object = vec![0x4F, 0x13];
    object.extend_from_slice(&u64::MAX.to_be_bytes());
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);

    // comprimento estendido sem marcador de inteiro
    let object = vec![0x4F, 0x50, 0x00];
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);

    // comprimento estendido de 16 bytes (marcador 0x14)
    let mut object = vec![0x5F, 0x14];
    object.extend_from_slice(&[0_u8; 16]);
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);

    // array com contagem absurda
    let mut object = vec![0xAF, 0x13];
    object.extend_from_slice(&u64::MAX.to_be_bytes());
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);

    // dict com contagem absurda
    let mut object = vec![0xDF, 0x13];
    object.extend_from_slice(&u64::MAX.to_be_bytes());
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);

    // string UTF-16 com comprimento que transborda (units * 2)
    let mut object = vec![0x6F, 0x13];
    object.extend_from_slice(&(u64::MAX / 2 + 1).to_be_bytes());
    assert_eq!(status_of(&common::encode(&[object], 0)), FgStatus::BadLength);
}

#[test]
fn cycles_are_detected() {
    // dict cujo valor aponta para ele mesmo
    let objects = vec![common::ascii("a"), common::dict(&[(0, 1)], 1)];
    let blob = common::encode(&objects, 1);
    let error = parse(&blob).expect_err("ciclo deveria ser recusado");
    assert_eq!(error.status(), FgStatus::Cycle);

    // array -> array -> array ... direto
    let objects = vec![common::array(&[0], 1)];
    let blob = common::encode(&objects, 0);
    assert_eq!(status_of(&blob), FgStatus::Cycle);
}

#[test]
fn depth_limit_is_64_like_the_cpp_parser() {
    fn chain(levels: usize) -> Vec<u8> {
        let mut objects: Vec<Vec<u8>> = Vec::new();
        for level in 0..levels {
            objects.push(common::array(&[level + 1], 1));
        }
        objects.push(common::null());
        common::encode(&objects, 0)
    }
    assert_eq!(status_of(&chain(64)), FgStatus::Ok);
    let error = parse(&chain(65)).expect_err("65 niveis deveria ser recusado");
    assert_eq!(error.status(), FgStatus::DepthLimit);
}

#[test]
fn payload_budget_stops_exponential_expansion() {
    // Um unico objeto data de 1 MiB referenciado 70 vezes: 70 MiB copiados > 64 MiB.
    let payload = vec![0xAB_u8; 1 << 20];
    let mut objects: Vec<Vec<u8>> = vec![common::data(&payload)];
    let references: Vec<usize> = std::iter::repeat_n(0, 70).collect();
    objects.push(common::array(&references, 1));
    let blob = common::encode(&objects, 1);
    let error = parse(&blob).expect_err("orcamento deveria estourar");
    assert_eq!(error.status(), FgStatus::BudgetExceeded);
}

#[test]
fn find_works_on_dicts() {
    let objects = vec![
        common::ascii("chave"),
        common::uint(1, 1),
        common::dict(&[(0, 1)], 1),
    ];
    let blob = common::encode(&objects, 2);
    let value = parse(&blob).expect("dict simples deveria parsear");
    assert!(matches!(value.find("chave"), Some(PlistValue::Integer(1))));
    assert!(value.find("outra").is_none());
    assert_eq!(value.len(), 1);
}

#[test]
fn empty_and_short_inputs_never_panic() {
    for length in 0..200_usize {
        let blob = vec![0x41_u8; length];
        let _ = parse(&blob);
        let mut prefixed = b"bplist00".to_vec();
        prefixed.extend(std::iter::repeat_n(0x00_u8, length));
        let _ = parse(&prefixed);
    }
}
