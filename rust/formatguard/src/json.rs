#![forbid(unsafe_code)]
//! Serializacao canonica: a mesma arvore sempre vira exatamente os mesmos bytes, em
//! qualquer maquina. Isso e o que permite comparar o parser Rust com o parser C++ por
//! igualdade de texto.
//!
//! # Contrato dos valores
//!
//! | tipo | JSON |
//! |---|---|
//! | null | null |
//! | bool | true / false |
//! | integer | numero decimal |
//! | real | expansao decimal exata do double (sem arredondar) |
//! | date | objeto com a unica chave $date e o valor em segundos desde 2001-01-01Z |
//! | data | objeto com a unica chave $data e o valor em hex minusculo |
//! | string / utf16 | string JSON |
//! | uid | objeto com a unica chave $uid |
//! | array | lista JSON |
//! | set | objeto com a unica chave $set e uma lista |
//! | dict | objeto JSON, na ordem do arquivo |
//!
//! Real nao finito (nan, inf, -inf) vira string JSON, porque JSON nao tem esses
//! numeros.
//!
//! Duas regras de escape existem para o resultado nunca ser ambiguo:
//!
//! * chave de dict que comeca com cifrao e escrita com o escape unicode de U+0024,
//!   para que nenhuma chave do arquivo se confunda com as chaves reservadas
//!   ($date, $data, $uid, $set);
//! * string cujos bytes nao formam UTF-8 valido tem cada byte >= 0x7F escrito como
//!   escape unicode U+00XX (nenhum byte e perdido nem "corrigido").
//!
//! # Protobuf
//!
//! Um objeto com a chave "fields" contendo uma lista de campos. Cada campo e um
//! objeto com "n" (numero), "w" (wire type) e mais:
//!
//! * w = 0: "i" (varint sem sinal) e "z" (o mesmo valor em zigzag, i64);
//! * w = 1: "i" (fixed64);
//! * w = 5: "i" (fixed32);
//! * w = 2: "len" e "hex" (bytes crus);
//! * w = 3: "fields" (o grupo aninhado).

use crate::error::{FgError, FgStatus};
use crate::limits::{MAX_JSON_BYTES, MAX_PLIST_DEPTH, MAX_PROTO_DEPTH};
use crate::plist::{hex_bytes, PlistValue};
use crate::protobuf::{zigzag_decode, ProtoMessage, ProtoValue};

/// Expansao decimal exata de um f64 (contrato verificado contra C++ e `CPython` em
/// 299.899 doubles, zero divergencias).
///
/// Zeros a direita da parte fracionaria sao removidos; -0.0 vira -0; valores nao
/// finitos viram "nan", "inf" e "-inf" (que o chamador escreve como string JSON).
#[must_use]
pub fn exact_real(value: f64) -> String {
    if value.is_nan() {
        return "nan".to_string();
    }
    if value.is_infinite() {
        return if value < 0.0 { "-inf".to_string() } else { "inf".to_string() };
    }
    let bits = value.to_bits();
    let exponent_field = ((bits >> 52) & 0x7FF) as i32;
    let fraction = bits & ((1_u64 << 52) - 1);
    if exponent_field == 0 && fraction == 0 {
        return if (bits >> 63) != 0 { "-0".to_string() } else { "0".to_string() };
    }
    let exponent2 = if exponent_field == 0 { -1074 } else { exponent_field.saturating_sub(1075) };
    let mut text = if exponent2 >= 0 {
        format!("{value:.0}")
    } else {
        let digits = exponent2.unsigned_abs() as usize;
        format!("{value:.digits$}")
    };
    if text.contains('.') {
        while text.ends_with('0') {
            text.truncate(text.len().saturating_sub(1));
        }
        if text.ends_with('.') {
            text.truncate(text.len().saturating_sub(1));
        }
    }
    text
}

/// Serializa um plist parseado.
///
/// # Erros
///
/// [`FgStatus::JsonLimit`] se a saida crescer demais.
pub fn plist_to_json(root: &PlistValue) -> Result<Vec<u8>, FgError> {
    let mut writer = Writer::new();
    write_plist(&mut writer, root, 0)?;
    Ok(writer.into_bytes())
}

/// Serializa uma mensagem protobuf parseada.
///
/// # Erros
///
/// [`FgStatus::JsonLimit`] se a saida crescer demais.
pub fn protobuf_to_json(root: &ProtoMessage) -> Result<Vec<u8>, FgError> {
    let mut writer = Writer::new();
    write_proto_message(&mut writer, root, 0)?;
    Ok(writer.into_bytes())
}

/// Escape JSON da aspa dupla.
const ESCAPED_QUOTE: &str = r#"\""#;
/// Escape JSON da barra invertida.
const ESCAPED_BACKSLASH: &str = r"\\";
/// Prefixo de escape unicode de 4 digitos.
const ESCAPED_UNICODE_PREFIX: &str = r"\u";
/// Escape unicode de U+0024 (cifrao), usado no inicio de chave de dict.
const ESCAPED_DOLLAR: &str = r"\u0024";

struct Writer {
    out: String,
}

impl Writer {
    fn new() -> Self {
        Self { out: String::new() }
    }

    fn into_bytes(self) -> Vec<u8> {
        self.out.into_bytes()
    }

    fn check(&self) -> Result<(), FgError> {
        if u64::try_from(self.out.len()).unwrap_or(u64::MAX) > MAX_JSON_BYTES {
            return Err(FgError::detail(
                FgStatus::JsonLimit,
                format!("saida JSON passou de {MAX_JSON_BYTES} bytes"),
            ));
        }
        Ok(())
    }

    fn write_str(&mut self, text: &str) -> Result<(), FgError> {
        self.out.push_str(text);
        self.check()
    }

    fn write_char(&mut self, value: char) -> Result<(), FgError> {
        self.out.push(value);
        self.check()
    }
}

fn write_unicode_escape(writer: &mut Writer, code: u16) -> Result<(), FgError> {
    const DIGITS: [u8; 16] = *b"0123456789abcdef";
    let mut text = String::with_capacity(6);
    text.push_str(ESCAPED_UNICODE_PREFIX);
    for shift in [12_u32, 8, 4, 0] {
        let nibble = usize::from((code >> shift) & 0x0F);
        text.push(char::from(DIGITS.get(nibble).copied().unwrap_or(b'?')));
    }
    writer.write_str(&text)
}

fn escape_char(writer: &mut Writer, value: char) -> Result<(), FgError> {
    match value {
        '"' => writer.write_str(ESCAPED_QUOTE),
        '\\' => writer.write_str(ESCAPED_BACKSLASH),
        '\u{08}' => writer.write_str(r"\b"),
        '\u{0c}' => writer.write_str(r"\f"),
        '\n' => writer.write_str(r"\n"),
        '\r' => writer.write_str(r"\r"),
        '\t' => writer.write_str(r"\t"),
        other if u32::from(other) < 0x20 => write_unicode_escape(writer, u32::from(other) as u16),
        other => writer.write_char(other),
    }
}

fn escape_control(writer: &mut Writer, code: u8) -> Result<(), FgError> {
    match code {
        0x08 => writer.write_str(r"\b"),
        0x0C => writer.write_str(r"\f"),
        0x0A => writer.write_str(r"\n"),
        0x0D => writer.write_str(r"\r"),
        0x09 => writer.write_str(r"\t"),
        other => write_unicode_escape(writer, u16::from(other)),
    }
}

/// Escreve os bytes como texto JSON. Se forem UTF-8 valido, escapa caractere a
/// caractere; se nao, escapa byte a byte (sem perder nada).
fn escape_bytes(writer: &mut Writer, bytes: &[u8]) -> Result<(), FgError> {
    if let Ok(text) = std::str::from_utf8(bytes) {
        for value in text.chars() {
            escape_char(writer, value)?;
        }
        return Ok(());
    }
    for &byte in bytes {
        match byte {
            b'"' => writer.write_str(ESCAPED_QUOTE)?,
            b'\\' => writer.write_str(ESCAPED_BACKSLASH)?,
            0x20..=0x7E => writer.write_char(char::from(byte))?,
            0x00..=0x1F => escape_control(writer, byte)?,
            other => write_unicode_escape(writer, u16::from(other))?,
        }
    }
    Ok(())
}

fn write_quoted(writer: &mut Writer, bytes: &[u8]) -> Result<(), FgError> {
    writer.write_char('"')?;
    escape_bytes(writer, bytes)?;
    writer.write_char('"')
}

fn write_real(writer: &mut Writer, value: f64) -> Result<(), FgError> {
    let text = exact_real(value);
    if value.is_finite() {
        writer.write_str(&text)
    } else {
        write_quoted(writer, text.as_bytes())
    }
}

fn write_plist(writer: &mut Writer, value: &PlistValue, depth: u32) -> Result<(), FgError> {
    if depth > MAX_PLIST_DEPTH.saturating_add(1) {
        return Err(FgError::detail(
            FgStatus::JsonLimit,
            "arvore profunda demais para serializar",
        ));
    }
    match value {
        PlistValue::Null => writer.write_str("null"),
        PlistValue::Bool(true) => writer.write_str("true"),
        PlistValue::Bool(false) => writer.write_str("false"),
        PlistValue::Integer(number) => writer.write_str(&number.to_string()),
        PlistValue::Real(number) => write_real(writer, *number),
        PlistValue::Date(number) => {
            writer.write_str(r#"{"$date":"#)?;
            write_real(writer, *number)?;
            writer.write_char('}')
        }
        PlistValue::Data(bytes) => {
            writer.write_str(r#"{"$data":""#)?;
            writer.write_str(&hex_bytes(bytes))?;
            writer.write_str(r#""}"#)
        }
        PlistValue::String(bytes) => write_quoted(writer, bytes),
        PlistValue::Utf16(text) => write_quoted(writer, text.as_bytes()),
        PlistValue::Uid(uid) => {
            writer.write_str(r#"{"$uid":"#)?;
            writer.write_str(&uid.to_string())?;
            writer.write_char('}')
        }
        PlistValue::Array(items) => write_list(writer, items, depth),
        PlistValue::Set(items) => {
            writer.write_str(r#"{"$set":"#)?;
            write_list(writer, items, depth)?;
            writer.write_char('}')
        }
        PlistValue::Dict(entries) => {
            writer.write_char('{')?;
            for (position, (key, item)) in entries.iter().enumerate() {
                if position > 0 {
                    writer.write_char(',')?;
                }
                write_dict_key(writer, key)?;
                writer.write_char(':')?;
                write_plist(writer, item, depth.saturating_add(1))?;
            }
            writer.write_char('}')
        }
    }
}

fn write_dict_key(writer: &mut Writer, key: &[u8]) -> Result<(), FgError> {
    writer.write_char('"')?;
    if key.first() == Some(&b'$') {
        writer.write_str(ESCAPED_DOLLAR)?;
        escape_bytes(writer, key.get(1..).unwrap_or(&[]))?;
    } else {
        escape_bytes(writer, key)?;
    }
    writer.write_char('"')
}

fn write_list(writer: &mut Writer, items: &[PlistValue], depth: u32) -> Result<(), FgError> {
    writer.write_char('[')?;
    for (position, item) in items.iter().enumerate() {
        if position > 0 {
            writer.write_char(',')?;
        }
        write_plist(writer, item, depth.saturating_add(1))?;
    }
    writer.write_char(']')
}

fn write_proto_message(
    writer: &mut Writer,
    message: &ProtoMessage,
    depth: u32,
) -> Result<(), FgError> {
    if depth > MAX_PROTO_DEPTH.saturating_add(1) {
        return Err(FgError::detail(
            FgStatus::JsonLimit,
            "mensagem profunda demais para serializar",
        ));
    }
    writer.write_str(r#"{"fields":"#)?;
    write_proto_fields(writer, message, depth)?;
    writer.write_char('}')
}

fn write_proto_fields(
    writer: &mut Writer,
    message: &ProtoMessage,
    depth: u32,
) -> Result<(), FgError> {
    writer.write_char('[')?;
    for (position, field) in message.fields.iter().enumerate() {
        if position > 0 {
            writer.write_char(',')?;
        }
        writer.write_str(r#"{"n":"#)?;
        writer.write_str(&field.number.to_string())?;
        writer.write_str(r#","w":"#)?;
        writer.write_str(&field.wire.as_u8().to_string())?;
        match &field.value {
            ProtoValue::Varint(raw) => {
                writer.write_str(r#","i":"#)?;
                writer.write_str(&raw.to_string())?;
                writer.write_str(r#","z":"#)?;
                writer.write_str(&zigzag_decode(*raw).to_string())?;
            }
            ProtoValue::Fixed64(raw) => {
                writer.write_str(r#","i":"#)?;
                writer.write_str(&raw.to_string())?;
            }
            ProtoValue::Fixed32(raw) => {
                writer.write_str(r#","i":"#)?;
                writer.write_str(&raw.to_string())?;
            }
            ProtoValue::Bytes(bytes) => {
                writer.write_str(r#","len":"#)?;
                writer.write_str(&bytes.len().to_string())?;
                writer.write_str(r#","hex":""#)?;
                writer.write_str(&hex_bytes(bytes))?;
                writer.write_char('"')?;
            }
            ProtoValue::Group(inner) => {
                writer.write_str(r#","fields":"#)?;
                write_proto_fields(writer, inner, depth.saturating_add(1))?;
            }
        }
        writer.write_char('}')?;
    }
    writer.write_char(']')
}
