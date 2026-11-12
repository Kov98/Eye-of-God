//! Testes da ABI C, chamada como o C++ chamaria (mesmas funcoes, mesmos ponteiros).
//!
//! As funcoes da ABI que recebem ponteiros crus sao `unsafe` do ponto de vista do Rust
//! (o C nao tem essa distincao). Os helpers abaixo encapsulam o contrato de cada chamada
//! num unico lugar: buffer vivo durante a chamada e saida gravavel.

mod common;

use formatguard::abi::{
    fg_abi_version, fg_free_plist, fg_free_proto, fg_free_string, fg_last_error, fg_parse_plist,
    fg_parse_protobuf, fg_plist_to_json, fg_proto_to_json, fg_strerror, fg_version, fg_plist,
    fg_proto,
};
use formatguard::error::FgStatus;
use std::ffi::CStr;
use std::os::raw::c_char;

fn parse_plist(data: &[u8]) -> (i32, *mut fg_plist) {
    let mut handle: *mut fg_plist = std::ptr::null_mut();
    // SAFETY: `data` aponta para `data.len()` bytes validos durante a chamada e
    // `handle` e memoria gravavel da stack.
    let status = unsafe { fg_parse_plist(data.as_ptr(), data.len(), &mut handle) };
    (status, handle)
}

fn parse_proto(data: &[u8]) -> (i32, *mut fg_proto) {
    let mut handle: *mut fg_proto = std::ptr::null_mut();
    // SAFETY: idem `parse_plist`.
    let status = unsafe { fg_parse_protobuf(data.as_ptr(), data.len(), &mut handle) };
    (status, handle)
}

fn plist_json(handle: *const fg_plist) -> (i32, Vec<u8>, *mut c_char) {
    let mut out: *mut c_char = std::ptr::null_mut();
    let mut out_len: usize = 0;
    // SAFETY: `handle` veio de `parse_plist` e continua vivo; `out`/`out_len` sao
    // enderecos validos da stack.
    let status = unsafe { fg_plist_to_json(handle, &mut out, &mut out_len) };
    let mut bytes = Vec::new();
    if status == 0 && !out.is_null() {
        // SAFETY: a biblioteca garante `out_len` bytes legiveis terminados em NUL.
        // SAFETY: leitura limitada a `out_len`, dentro do buffer alocado.
        bytes = unsafe { std::slice::from_raw_parts(out.cast::<u8>(), out_len) }.to_vec();
        // SAFETY: o byte `out_len` e o NUL terminador garantido pelo contrato.
        assert_eq!(unsafe { *out.add(out_len) }, 0, "JSON deve terminar em NUL");
    }
    (status, bytes, out)
}

fn proto_json(handle: *const fg_proto) -> (i32, Vec<u8>, *mut c_char) {
    let mut out: *mut c_char = std::ptr::null_mut();
    let mut out_len: usize = 0;
    // SAFETY: idem `plist_json`.
    let status = unsafe { fg_proto_to_json(handle, &mut out, &mut out_len) };
    let mut bytes = Vec::new();
    if status == 0 && !out.is_null() {
        // SAFETY: leitura limitada a `out_len`.
        bytes = unsafe { std::slice::from_raw_parts(out.cast::<u8>(), out_len) }.to_vec();
    }
    (status, bytes, out)
}

fn free_plist(handle: *mut fg_plist) {
    // SAFETY: `handle` veio de `parse_plist` e e liberado uma unica vez; a funcao aceita nulo.
    unsafe { fg_free_plist(handle) };
}

fn free_proto(handle: *mut fg_proto) {
    // SAFETY: idem `free_plist`.
    unsafe { fg_free_proto(handle) };
}

fn free_string(text: *mut c_char) {
    // SAFETY: `text` veio de `plist_json`/`proto_json` e e liberado uma unica vez.
    unsafe { fg_free_string(text) };
}

fn strerror_text(code: i32) -> String {
    // SAFETY: fg_strerror devolve ponteiro estatico, terminado em NUL.
    unsafe { CStr::from_ptr(fg_strerror(code)) }.to_string_lossy().into_owned()
}

#[test]
fn parse_and_serialize_a_plist_through_the_c_abi() {
    let objects = vec![
        common::ascii("chave"),
        common::integer(7),
        common::dict(&[(0, 1)], 1),
    ];
    let blob = common::encode(&objects, 2);
    let (status, handle) = parse_plist(&blob);
    assert_eq!(status, 0, "parse deveria passar: {}", strerror_text(status));
    assert!(!handle.is_null());

    let (status, json, raw) = plist_json(handle);
    assert_eq!(status, 0);
    assert_eq!(String::from_utf8_lossy(&json), r#"{"chave":7}"#);

    free_string(raw);
    free_plist(handle);
}

#[test]
fn null_and_empty_arguments_return_codes() {
    let mut handle: *mut fg_plist = std::ptr::null_mut();
    // out nulo
    // SAFETY: exercita justamente o caminho de recusa por ponteiro nulo.
    assert_eq!(
        unsafe { fg_parse_plist(b"bplist00".as_ptr(), 8, std::ptr::null_mut()) },
        FgStatus::NullArgument.as_i32()
    );
    // data nulo com len > 0
    // SAFETY: `data` nulo e recusado antes de qualquer leitura.
    assert_eq!(
        unsafe { fg_parse_plist(std::ptr::null(), 8, &mut handle) },
        FgStatus::NullArgument.as_i32()
    );
    assert!(handle.is_null());
    // data nulo com len == 0 -> entrada vazia, sem derefenciar nada
    // SAFETY: len 0 nao produz leitura de memoria.
    assert_eq!(
        unsafe { fg_parse_plist(std::ptr::null(), 0, &mut handle) },
        FgStatus::EmptyInput.as_i32()
    );
    assert!(handle.is_null());
    // lixo
    let junk = [0x55_u8; 64];
    let (status, handle) = parse_plist(&junk);
    assert!(status < 0);
    assert!(handle.is_null());

    // JSON de handle nulo
    let mut json: *mut c_char = std::ptr::null_mut();
    let mut json_len: usize = 0;
    // SAFETY: handle nulo e recusado antes de qualquer dereferencia.
    assert_eq!(
        unsafe { fg_plist_to_json(std::ptr::null(), &mut json, &mut json_len) },
        FgStatus::NullArgument.as_i32()
    );
    assert!(json.is_null());
    assert_eq!(json_len, 0);
    // out_len nulo
    // SAFETY: ponteiro nulo recusado antes de escrever.
    assert_eq!(
        unsafe { fg_plist_to_json(std::ptr::null(), &mut json, std::ptr::null_mut()) },
        FgStatus::NullArgument.as_i32()
    );

    // frees aceitam nulo
    free_plist(std::ptr::null_mut());
    free_proto(std::ptr::null_mut());
    free_string(std::ptr::null_mut());
}

#[test]
fn protobuf_empty_buffer_is_ok_and_json_roundtrips() {
    let (status, handle) = parse_proto(&[]);
    assert_eq!(status, 0, "buffer vazio e mensagem vazia");
    assert!(!handle.is_null());
    let (status, json, raw) = proto_json(handle);
    assert_eq!(status, 0);
    assert_eq!(String::from_utf8_lossy(&json), r#"{"fields":[]}"#);
    free_string(raw);
    free_proto(handle);
}

#[test]
fn messages_and_version_are_available() {
    assert_eq!(fg_abi_version(), formatguard::abi::ABI_VERSION);
    assert_eq!(fg_abi_version(), 1);
    // SAFETY: fg_version devolve ponteiro estatico, terminado em NUL.
    let version = unsafe { CStr::from_ptr(fg_version()) }.to_string_lossy().into_owned();
    assert!(version.starts_with("formatguard "), "versao: {version}");
    assert_eq!(strerror_text(0), "ok");
    assert_eq!(strerror_text(FgStatus::NullArgument.as_i32()), "ponteiro nulo recusado");
    assert!(!strerror_text(FgStatus::Cycle.as_i32()).is_empty());
    assert_eq!(strerror_text(-12345), "codigo desconhecido");
}

#[test]
fn last_error_is_filled_only_on_failure() {
    let ok_blob = common::encode(&[common::null()], 0);
    let (status, handle) = parse_plist(&ok_blob);
    assert_eq!(status, 0);
    free_plist(handle);

    let junk = [0x55_u8; 64];
    let (status, handle) = parse_plist(&junk);
    assert!(status < 0);
    assert!(handle.is_null());
    // SAFETY: fg_last_error devolve ponteiro por thread, terminado em NUL, valido ate a
    // proxima falha nesta thread.
    let detail = unsafe { CStr::from_ptr(fg_last_error()) }.to_string_lossy().into_owned();
    assert!(!detail.is_empty(), "detalhe do ultimo erro ficou vazio");
}

#[test]
fn fuzzing_the_abi_never_crashes() {
    let mut state: u64 = 0xDEAD_BEEF_1234_5678;
    let mut accepted = 0_u64;
    let mut rejected = 0_u64;
    for iteration in 0..5000_u64 {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        let length = usize::try_from(state % 96).unwrap_or(0);
        let mut data: Vec<u8> = Vec::with_capacity(length);
        for _ in 0..length {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            data.push(u8::try_from(state & 0xFF).unwrap_or(0));
        }
        if iteration % 3 == 0 {
            data.splice(0..0, *b"bplist00");
        }

        let (status, plist) = parse_plist(&data);
        if status == 0 {
            accepted += 1;
            assert!(!plist.is_null());
            let (json_status, json, raw) = plist_json(plist);
            assert_eq!(json_status, 0);
            assert!(!json.is_empty());
            free_string(raw);
        } else {
            rejected += 1;
            assert!(plist.is_null());
            assert!(FgStatus::from_i32(status).is_some(), "codigo fora da tabela: {status}");
        }
        free_plist(plist);

        let (status, proto) = parse_proto(&data);
        if status == 0 {
            assert!(!proto.is_null());
            let (json_status, _, raw) = proto_json(proto);
            assert_eq!(json_status, 0);
            free_string(raw);
        } else {
            assert!(proto.is_null());
            assert!(FgStatus::from_i32(status).is_some());
        }
        free_proto(proto);
    }
    assert_eq!(accepted + rejected, 5000);
    println!("abi fuzz: {accepted} aceitas, {rejected} recusadas");
}
