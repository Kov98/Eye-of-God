//! Construtor de binary plists usado pelos testes de integracao.
//!
//! Nao depende do parser: monta os bytes a mao, para que um erro do parser nao possa
//! "consertar" o vetor de teste.
#![allow(dead_code)]

/// Largura minima de inteiro que cabe em `value`.
fn width_for(value: u64) -> usize {
    if value <= 0xFF {
        1
    } else if value <= 0xFFFF {
        2
    } else if value <= 0xFFFF_FFFF {
        4
    } else {
        8
    }
}

/// Marcador de inteiro (`0x1n`) para uma largura em bytes.
fn int_marker(width: usize) -> u8 {
    match width {
        1 => 0x10,
        2 => 0x11,
        4 => 0x12,
        8 => 0x13,
        _ => 0x13,
    }
}

/// Escreve `value` em `width` bytes big-endian.
pub fn push_be(out: &mut Vec<u8>, value: u64, width: usize) {
    let bytes = value.to_be_bytes();
    let start = 8usize.saturating_sub(width);
    for byte in bytes.get(start..).unwrap_or(&[]) {
        out.push(*byte);
    }
}

/// Marcador com comprimento: direto quando < 15, objeto inteiro depois.
fn with_length(marker: u8, count: usize, width: usize) -> Vec<u8> {
    let mut out = Vec::new();
    if count < 15 {
        out.push(marker | u8::try_from(count).unwrap_or(0));
        return out;
    }
    out.push(marker | 0x0F);
    out.push(int_marker(width));
    push_be(&mut out, u64::try_from(count).unwrap_or(0), width);
    out
}

/// `0x00`.
pub fn null() -> Vec<u8> {
    vec![0x00]
}

/// `0x08` / `0x09`.
pub fn boolean(value: bool) -> Vec<u8> {
    vec![if value { 0x09 } else { 0x08 }]
}

/// Inteiro de `width` bytes.
pub fn uint(value: u64, width: usize) -> Vec<u8> {
    let mut out = vec![int_marker(width)];
    push_be(&mut out, value, width);
    out
}

/// Inteiro de 8 bytes com sinal (complemento de dois).
pub fn integer(value: i64) -> Vec<u8> {
    let mut out = vec![0x13];
    out.extend_from_slice(&value.to_be_bytes());
    out
}

/// Real de 8 bytes.
pub fn real64(value: f64) -> Vec<u8> {
    let mut out = vec![0x23];
    out.extend_from_slice(&value.to_bits().to_be_bytes());
    out
}

/// Real de 4 bytes.
pub fn real32(value: f32) -> Vec<u8> {
    let mut out = vec![0x22];
    out.extend_from_slice(&value.to_bits().to_be_bytes());
    out
}

/// Data Apple (`0x33`).
pub fn date(seconds_since_2001: f64) -> Vec<u8> {
    let mut out = vec![0x33];
    out.extend_from_slice(&seconds_since_2001.to_bits().to_be_bytes());
    out
}

/// Objeto data.
pub fn data(bytes: &[u8]) -> Vec<u8> {
    let mut out = with_length(0x40, bytes.len(), width_for(u64::try_from(bytes.len()).unwrap_or(0)));
    out.extend_from_slice(bytes);
    out
}

/// Objeto string ASCII (bytes crus).
pub fn ascii_bytes(bytes: &[u8]) -> Vec<u8> {
    let mut out = with_length(0x50, bytes.len(), width_for(u64::try_from(bytes.len()).unwrap_or(0)));
    out.extend_from_slice(bytes);
    out
}

/// Objeto string ASCII (texto UTF-8).
pub fn ascii(text: &str) -> Vec<u8> {
    ascii_bytes(text.as_bytes())
}

/// Objeto string UTF-16BE (surrogate pairs incluidos).
pub fn utf16(text: &str) -> Vec<u8> {
    let units: Vec<u16> = text.encode_utf16().collect();
    let mut out = with_length(0x60, units.len(), width_for(u64::try_from(units.len()).unwrap_or(0)));
    for unit in units {
        out.extend_from_slice(&unit.to_be_bytes());
    }
    out
}

/// Objeto uid de `width` bytes.
pub fn uid(value: u64, width: usize) -> Vec<u8> {
    let mut out = vec![0x80 | u8::try_from(width.saturating_sub(1)).unwrap_or(0)];
    push_be(&mut out, value, width);
    out
}

fn refs_object(marker: u8, refs: &[usize], ref_size: usize) -> Vec<u8> {
    let mut out = with_length(marker, refs.len(), width_for(u64::try_from(refs.len()).unwrap_or(0)));
    for reference in refs {
        push_be(&mut out, u64::try_from(*reference).unwrap_or(0), ref_size);
    }
    out
}

/// Objeto array (`0xA`).
pub fn array(refs: &[usize], ref_size: usize) -> Vec<u8> {
    refs_object(0xA0, refs, ref_size)
}

/// Objeto set (`0xC`).
pub fn set_of(refs: &[usize], ref_size: usize) -> Vec<u8> {
    refs_object(0xC0, refs, ref_size)
}

/// Objeto dict (`0xD`): pares (ref da chave, ref do valor).
pub fn dict(pairs: &[(usize, usize)], ref_size: usize) -> Vec<u8> {
    let mut out = with_length(
        0xD0,
        pairs.len(),
        width_for(u64::try_from(pairs.len()).unwrap_or(0)),
    );
    for (key, _) in pairs {
        push_be(&mut out, u64::try_from(*key).unwrap_or(0), ref_size);
    }
    for (_, value) in pairs {
        push_be(&mut out, u64::try_from(*value).unwrap_or(0), ref_size);
    }
    out
}

/// Codifica com tabela de offsets/refs do tamanho minimo.
pub fn encode(objects: &[Vec<u8>], top: usize) -> Vec<u8> {
    let max_offset: usize = objects.iter().map(Vec::len).sum();
    let offset_size = width_for(u64::try_from(max_offset).unwrap_or(0));
    let ref_size = width_for(u64::try_from(objects.len().saturating_sub(1)).unwrap_or(0));
    encode_with(objects, top, offset_size, ref_size)
}

/// Codifica com tamanhos explicitos (para testar trailers hostis).
pub fn encode_with(objects: &[Vec<u8>], top: usize, offset_size: usize, ref_size: usize) -> Vec<u8> {
    let mut out: Vec<u8> = b"bplist00".to_vec();
    let mut offsets: Vec<u64> = Vec::new();
    for object in objects {
        offsets.push(u64::try_from(out.len()).unwrap_or(0));
        out.extend_from_slice(object);
    }
    let offset_table = u64::try_from(out.len()).unwrap_or(0);
    for offset in &offsets {
        push_be(&mut out, *offset, offset_size);
    }
    let mut trailer = [0_u8; 32];
    trailer[6] = u8::try_from(offset_size).unwrap_or(0);
    trailer[7] = u8::try_from(ref_size).unwrap_or(0);
    trailer[8..16].copy_from_slice(&u64::try_from(objects.len()).unwrap_or(0).to_be_bytes());
    trailer[16..24].copy_from_slice(&u64::try_from(top).unwrap_or(0).to_be_bytes());
    trailer[24..32].copy_from_slice(&offset_table.to_be_bytes());
    out.extend_from_slice(&trailer);
    out
}

/// JSON canonico de um plist codificado.
pub fn json_of(objects: &[Vec<u8>], top: usize) -> String {
    let blob = encode(objects, top);
    let value = formatguard::plist::parse(&blob).expect("plist deveria parsear");
    let json = formatguard::json::plist_to_json(&value).expect("json deveria sair");
    String::from_utf8_lossy(&json).into_owned()
}
