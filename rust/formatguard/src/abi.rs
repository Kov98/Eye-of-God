//! ABI C estreita. E o **unico** modulo deste crate que usa `unsafe`, e cada bloco
//! `unsafe` existe por um motivo declarado em comentario `SAFETY`.
//!
//! Contrato (o mesmo de `include/formatguard.h`):
//!
//! * toda funcao devolve `i32`: 0 = sucesso, negativo = [`FgStatus`];
//! * *out sempre nasce `NULL`/`0` e so muda em caso de sucesso;
//! * memoria devolvida e liberada por `fg_free_plist`/`fg_free_proto`/`fg_free_string`,
//!   nunca por `free()` do CRT do chamador;
//! * nenhuma funcao usa estado global mutavel, exceto o detalhe do ultimo erro, que e
//!   por thread.
#![allow(unsafe_code)]
// Os nomes dos tipos opacos sao os da ABI C, de proposito.
#![allow(non_camel_case_types)]

use std::cell::RefCell;
use std::ffi::CString;
use std::os::raw::c_char;
use std::panic::{catch_unwind, UnwindSafe};

use crate::error::{FgError, FgStatus};
use crate::limits::MAX_INPUT_BYTES;
use crate::plist::PlistValue;
use crate::protobuf::ProtoMessage;

/// Versao da ABI (incrementada quando o contrato muda de forma incompativel).
pub const ABI_VERSION: i32 = 1;

/// Resultado opaco de [`fg_parse_plist`]. O chamador nunca ve o conteudo.
pub struct fg_plist {
    value: PlistValue,
}

/// Resultado opaco de [`fg_parse_protobuf`]. O chamador nunca ve o conteudo.
pub struct fg_proto {
    value: ProtoMessage,
}

thread_local! {
    static LAST_ERROR: RefCell<CString> = RefCell::new(CString::default());
}

fn set_last_error(message: &str) {
    let mut bytes: Vec<u8> = message.bytes().filter(|byte| *byte != 0).collect();
    bytes.truncate(4096);
    let text = CString::new(bytes).unwrap_or_default();
    LAST_ERROR.with(|cell| {
        *cell.borrow_mut() = text;
    });
}

fn panic_detail(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(text) = payload.downcast_ref::<&str>() {
        return (*text).to_string();
    }
    if let Some(text) = payload.downcast_ref::<String>() {
        return text.clone();
    }
    "causa desconhecida".to_string()
}

/// Converte `Result` em codigo, traduzindo panic em [`FgStatus::Internal`] em vez de
/// deixar a excecao atravessar a fronteira C.
fn guard<F>(body: F) -> i32
where
    F: FnOnce() -> Result<(), FgError> + UnwindSafe,
{
    match catch_unwind(body) {
        Ok(Ok(())) => FgStatus::Ok.as_i32(),
        Ok(Err(error)) => {
            set_last_error(&error.message());
            error.status().as_i32()
        }
        Err(payload) => {
            let detail = panic_detail(payload.as_ref());
            set_last_error(&format!("panic capturado na fronteira C: {detail}"));
            FgStatus::Internal.as_i32()
        }
    }
}

/// # Safety
///
/// `data` precisa apontar para `len` bytes legiveis (ou ser nulo com `len == 0`).
unsafe fn input_slice<'a>(data: *const u8, len: usize) -> Result<&'a [u8], FgError> {
    if len == 0 {
        return Ok(&[]);
    }
    if data.is_null() {
        return Err(FgError::detail(
            FgStatus::NullArgument,
            format!("ponteiro nulo com {len} bytes declarados"),
        ));
    }
    let declared = u64::try_from(len).unwrap_or(u64::MAX);
    if declared > MAX_INPUT_BYTES {
        return Err(FgError::detail(
            FgStatus::InputTooLarge,
            format!("{len} bytes acima do limite de {MAX_INPUT_BYTES}"),
        ));
    }
    if len > isize::MAX.unsigned_abs() {
        return Err(FgError::detail(FgStatus::InputTooLarge, "len nao cabe em isize"));
    }
    // SAFETY: o chamador garante `len` bytes legiveis; `len` ja foi checado contra
    // MAX_INPUT_BYTES e contra isize::MAX, entao `from_raw_parts` recebe um tamanho
    // valido para a plataforma.
    Ok(unsafe { std::slice::from_raw_parts(data, len) })
}

/// Parseia um Apple binary plist.
///
/// Devolve 0 e `*out` preenchido em caso de sucesso, ou codigo negativo e `*out` nulo.
///
/// # Safety
///
/// `data`/`len` tem que descrever um buffer valido e `out` tem que apontar para memoria
/// gravavel de um ponteiro.
#[no_mangle]
pub unsafe extern "C" fn fg_parse_plist(data: *const u8, len: usize, out: *mut *mut fg_plist) -> i32 {
    guard(move || {
        if out.is_null() {
            return Err(FgError::new(FgStatus::NullArgument));
        }
        // SAFETY: `out` nao e nulo (checado acima) e o chamador garante que e gravavel.
        unsafe {
            *out = std::ptr::null_mut();
        }
        // SAFETY: contrato de `input_slice` garantido pelo chamador da ABI.
        let bytes = unsafe { input_slice(data, len)? };
        if bytes.is_empty() {
            return Err(FgError::detail(
                FgStatus::EmptyInput,
                "zero bytes nao e um binary plist",
            ));
        }
        let value = crate::plist::parse(bytes)?;
        let handle = Box::into_raw(Box::new(fg_plist { value }));
        // SAFETY: idem acima.
        unsafe {
            *out = handle;
        }
        Ok(())
    })
}

/// Parseia um buffer protobuf.
///
/// # Safety
///
/// `data`/`len` tem que descrever um buffer valido e `out` tem que apontar para memoria
/// gravavel de um ponteiro.
#[no_mangle]
pub unsafe extern "C" fn fg_parse_protobuf(data: *const u8, len: usize, out: *mut *mut fg_proto) -> i32 {
    guard(move || {
        if out.is_null() {
            return Err(FgError::new(FgStatus::NullArgument));
        }
        // SAFETY: ver `fg_parse_plist`.
        unsafe {
            *out = std::ptr::null_mut();
        }
        let bytes = unsafe { input_slice(data, len)? };
        let value = crate::protobuf::parse(bytes)?;
        let handle = Box::into_raw(Box::new(fg_proto { value }));
        unsafe {
            *out = handle;
        }
        Ok(())
    })
}

/// Libera um resultado de [`fg_parse_plist`]. Aceita nulo.
///
/// # Safety
///
/// `handle` tem que ser um ponteiro devolvido por [`fg_parse_plist`] e ainda nao liberado.
#[no_mangle]
pub unsafe extern "C" fn fg_free_plist(handle: *mut fg_plist) {
    if handle.is_null() {
        return;
    }
    // SAFETY: contrato da ABI: o ponteiro veio de `Box::into_raw` de `fg_parse_plist`.
    unsafe {
        drop(Box::from_raw(handle));
    }
}

/// Libera um resultado de [`fg_parse_protobuf`]. Aceita nulo.
///
/// # Safety
///
/// `handle` tem que ser um ponteiro devolvido por [`fg_parse_protobuf`] e ainda nao liberado.
#[no_mangle]
pub unsafe extern "C" fn fg_free_proto(handle: *mut fg_proto) {
    if handle.is_null() {
        return;
    }
    // SAFETY: contrato da ABI.
    unsafe {
        drop(Box::from_raw(handle));
    }
}

/// Libera um buffer de JSON devolvido por [`fg_plist_to_json`] ou [`fg_proto_to_json`].
/// Aceita nulo.
///
/// # Safety
///
/// `text` tem que ser um ponteiro devolvido por esta biblioteca e ainda nao liberado.
#[no_mangle]
pub unsafe extern "C" fn fg_free_string(text: *mut c_char) {
    if text.is_null() {
        return;
    }
    // SAFETY: contrato da ABI: o ponteiro veio de `CString::into_raw`.
    unsafe {
        drop(CString::from_raw(text));
    }
}

fn json_to_c(json: Vec<u8>, out: *mut *mut c_char, out_len: *mut usize) -> Result<(), FgError> {
    let length = json.len();
    let text = CString::new(json).map_err(|_| {
        FgError::detail(FgStatus::Internal, "JSON conteve byte nulo (nao deveria acontecer)")
    })?;
    let raw = text.into_raw();
    // SAFETY: `out` e `out_len` foram checados como nao nulos pelo chamador.
    unsafe {
        *out = raw;
        *out_len = length;
    }
    Ok(())
}

/// Serializa um plist em JSON (UTF-8, terminado em NUL; `*out_len` nao conta o NUL).
///
/// # Safety
///
/// `handle` tem que ser valido (ou nulo, o que devolve erro) e `out`/`out_len` tem que
/// apontar para memoria gravavel.
#[no_mangle]
pub unsafe extern "C" fn fg_plist_to_json(
    handle: *const fg_plist,
    out: *mut *mut c_char,
    out_len: *mut usize,
) -> i32 {
    guard(move || {
        if handle.is_null() || out.is_null() || out_len.is_null() {
            return Err(FgError::new(FgStatus::NullArgument));
        }
        // SAFETY: ponteiros checados acima.
        unsafe {
            *out = std::ptr::null_mut();
            *out_len = 0;
        }
        // SAFETY: contrato da ABI: `handle` veio de `fg_parse_plist` e continua vivo.
        let value = unsafe { &(*handle).value };
        let json = crate::json::plist_to_json(value)?;
        json_to_c(json, out, out_len)
    })
}

/// Serializa uma mensagem protobuf em JSON.
///
/// # Safety
///
/// `handle` tem que ser valido (ou nulo, o que devolve erro) e `out`/`out_len` tem que
/// apontar para memoria gravavel.
#[no_mangle]
pub unsafe extern "C" fn fg_proto_to_json(
    handle: *const fg_proto,
    out: *mut *mut c_char,
    out_len: *mut usize,
) -> i32 {
    guard(move || {
        if handle.is_null() || out.is_null() || out_len.is_null() {
            return Err(FgError::new(FgStatus::NullArgument));
        }
        unsafe {
            *out = std::ptr::null_mut();
            *out_len = 0;
        }
        let value = unsafe { &(*handle).value };
        let json = crate::json::protobuf_to_json(value)?;
        json_to_c(json, out, out_len)
    })
}

/// Mensagem fixa de um codigo. O ponteiro e estatico: nunca liberar.
#[no_mangle]
pub extern "C" fn fg_strerror(code: i32) -> *const c_char {
    match FgStatus::from_i32(code) {
        Some(FgStatus::Ok) => c"ok".as_ptr(),
        Some(FgStatus::NullArgument) => c"ponteiro nulo recusado".as_ptr(),
        Some(FgStatus::EmptyInput) => c"entrada vazia".as_ptr(),
        Some(FgStatus::InputTooLarge) => c"entrada maior que o limite da ABI".as_ptr(),
        Some(FgStatus::BadSignature) => c"assinatura bplist00 ausente".as_ptr(),
        Some(FgStatus::Truncated) => c"buffer truncado".as_ptr(),
        Some(FgStatus::BadTrailer) => c"trailer do binary plist invalido".as_ptr(),
        Some(FgStatus::BadOffsetTable) => c"tabela de offsets invalida".as_ptr(),
        Some(FgStatus::BadMarker) => c"marcador de tipo invalido".as_ptr(),
        Some(FgStatus::BadLength) => c"comprimento maior que o buffer".as_ptr(),
        Some(FgStatus::DepthLimit) => c"aninhamento acima do limite".as_ptr(),
        Some(FgStatus::Cycle) => c"referencia circular".as_ptr(),
        Some(FgStatus::BudgetExceeded) => c"orcamento de nos ou bytes estourado".as_ptr(),
        Some(FgStatus::BadReference) => c"referencia a objeto inexistente".as_ptr(),
        Some(FgStatus::Unsupported) => c"tipo ou largura nao suportada".as_ptr(),
        Some(FgStatus::BadUtf16) => c"UTF-16 invalido".as_ptr(),
        Some(FgStatus::BadVarint) => c"varint invalido ou truncado".as_ptr(),
        Some(FgStatus::BadWireType) => c"wire type protobuf desconhecido".as_ptr(),
        Some(FgStatus::BadFieldNumber) => c"numero de campo protobuf invalido".as_ptr(),
        Some(FgStatus::BadGroup) => c"grupo protobuf mal formado".as_ptr(),
        Some(FgStatus::JsonLimit) => c"saida JSON maior que o limite".as_ptr(),
        Some(FgStatus::Internal) => c"falha interna".as_ptr(),
        None => c"codigo desconhecido".as_ptr(),
    }
}

/// Detalhe textual do ultimo erro **desta thread**. O ponteiro vale ate a proxima
/// chamada que falhe na mesma thread; nunca liberar.
#[no_mangle]
pub extern "C" fn fg_last_error() -> *const c_char {
    LAST_ERROR.with(|cell| cell.borrow().as_ptr())
}

/// Versao da biblioteca, texto estatico. Nunca liberar.
#[no_mangle]
pub extern "C" fn fg_version() -> *const c_char {
    c"formatguard 0.1.0".as_ptr()
}

/// Versao da ABI. Nunca liberar (nao devolve ponteiro).
#[no_mangle]
pub extern "C" fn fg_abi_version() -> i32 {
    ABI_VERSION
}
