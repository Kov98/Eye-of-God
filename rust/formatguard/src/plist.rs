#![forbid(unsafe_code)]
// Um braco por marcador do formato: partir `parse_body` em pedacos menores so
// esconderia o mapeamento 1:1 com a especificacao.
#![allow(clippy::too_many_lines)]
//! Apple binary plist (`bplist00`) para entrada nao confiavel.
//!
//! O parser segue o mesmo contrato de aceitacao do parser C++ de referencia
//! (`ios_backup.cpp`): assinatura, trailer de 32 bytes, tabela de offsets, e cada
//! objeto e lido a partir do seu offset. As diferencas sao todas restritivas:
//!
//! * referencia circular e recusada ([`FgStatus::Cycle`]) em vez de estourar a pilha no
//!   limite de profundidade;
//! * orcamento de nos e de bytes copiados ([`crate::limits`]) impede a expansao
//!   exponencial de um grafo de referencias compartilhadas;
//! * `units * 2` da string UTF-16 e checado com `checked_mul`.
//!
//! # Formato coberto
//!
//! | marcador | tipo | variante |
//! |---|---|---|
//! | `0x0n` | null/bool | [`PlistValue::Null`], [`PlistValue::Bool`] |
//! | `0x1n` | inteiro 1/2/4/8/16 bytes | [`PlistValue::Integer`] |
//! | `0x2n` | real float32/float64 | [`PlistValue::Real`] |
//! | `0x33` | data Apple (segundos desde 2001-01-01Z) | [`PlistValue::Date`] |
//! | `0x4n` | data | [`PlistValue::Data`] |
//! | `0x5n` | string ASCII | [`PlistValue::String`] |
//! | `0x6n` | string UTF-16BE | [`PlistValue::Utf16`] |
//! | `0x8n` | uid | [`PlistValue::Uid`] |
//! | `0xAn`, `0xBn` | array | [`PlistValue::Array`] |
//! | `0xCn` | set | [`PlistValue::Set`] |
//! | `0xDn` | dict | [`PlistValue::Dict`] |

use crate::error::{hex_byte, FgError, FgStatus};
use crate::limits::{
    Budget, MAX_PLIST_DEPTH, MAX_PLIST_NODES, MAX_PLIST_PAYLOAD, RESERVE_HINT,
};

/// Assinatura obrigatoria dos binary plists.
pub const SIGNATURE: &[u8] = b"bplist00";

/// Tamanho do trailer (sempre os 32 ultimos bytes).
const TRAILER_SIZE: usize = 32;

/// Um valor de binary plist ja validado.
#[derive(Debug, Clone, PartialEq)]
pub enum PlistValue {
    /// `0x00` / `0x0F`.
    Null,
    /// `0x08` (falso) / `0x09` (verdadeiro).
    Bool(bool),
    /// `0x1n`. Larguras de 1, 2, 4 e 8 bytes; 16 bytes e recusado (nao cabe em i64).
    Integer(i64),
    /// `0x2n` com n = 2 ou 3 (float32/float64).
    Real(f64),
    /// `0x33`: segundos desde 2001-01-01T00:00:00Z, exatamente como armazenado.
    Date(f64),
    /// `0x4n`.
    Data(Vec<u8>),
    /// `0x5n`: os bytes exatamente como estao no arquivo. O formato se chama
    /// "ASCII", mas um arquivo hostil pode guardar qualquer byte, entao o parser
    /// nao valida UTF-8 nem descarta nada.
    String(Vec<u8>),
    /// `0x6n`: UTF-16BE convertido para UTF-8. Surrogates soltos viram U+FFFD
    /// (mesmo resultado do `WideCharToMultiByte` do Windows usado pelo C++).
    Utf16(String),
    /// `0x8n`: uid do `NSKeyedArchiver`.
    Uid(u64),
    /// `0xAn` ou `0xBn`.
    Array(Vec<PlistValue>),
    /// `0xCn`.
    Set(Vec<PlistValue>),
    /// `0xDn`: pares na ordem do arquivo, com a chave em bytes crus (a mesma
    /// representacao que o C++ de referencia usa). Chaves repetidas sao mantidas.
    Dict(Vec<(Vec<u8>, PlistValue)>),
}

impl PlistValue {
    /// Nome do tipo, igual ao `plist_kind_name` do C++ (para as chaves de dict).
    #[must_use]
    pub fn kind_name(&self) -> &'static str {
        match self {
            Self::Null => "null",
            Self::Bool(_) => "bool",
            Self::Integer(_) => "integer",
            Self::Real(_) => "real",
            Self::Date(_) => "date",
            Self::Data(_) => "data",
            Self::String(_) | Self::Utf16(_) => "string",
            Self::Uid(_) => "uid",
            Self::Array(_) => "array",
            Self::Set(_) => "set",
            Self::Dict(_) => "dict",
        }
    }

    /// Busca uma chave textual em um dict (comparacao byte a byte).
    #[must_use]
    pub fn find(&self, key: &str) -> Option<&Self> {
        let Self::Dict(entries) = self else { return None };
        entries
            .iter()
            .find(|(name, _)| name.as_slice() == key.as_bytes())
            .map(|(_, value)| value)
    }

    /// Quantidade de elementos (array/set/dict) ou 0.
    #[must_use]
    pub fn len(&self) -> usize {
        match self {
            Self::Array(items) | Self::Set(items) => items.len(),
            Self::Dict(entries) => entries.len(),
            _ => 0,
        }
    }

    /// Verdadeiro quando o valor nao tem filhos.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// Parseia um `bplist00` inteiro. Nao modifica `data` e nao guarda referencia a ele.
///
/// # Erros
///
/// Devolve [`FgError`] com [`FgStatus`] estavel para qualquer entrada invalida.
pub fn parse(data: &[u8]) -> Result<PlistValue, FgError> {
    Parser::new(data).run()
}

/// Estado do parsing de um blob.
struct Parser<'a> {
    data: &'a [u8],
    offset_size: usize,
    ref_size: usize,
    object_count: u64,
    top_object: u64,
    offset_table: u64,
    offsets: Vec<u64>,
    /// Indices atualmente sendo expandidos (deteccao de ciclo).
    stack: Vec<u64>,
    budget: Budget,
}

impl<'a> Parser<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self {
            data,
            offset_size: 0,
            ref_size: 0,
            object_count: 0,
            top_object: 0,
            offset_table: 0,
            offsets: Vec::new(),
            stack: Vec::new(),
            budget: Budget::new(MAX_PLIST_NODES, MAX_PLIST_PAYLOAD),
        }
    }

    fn run(mut self) -> Result<PlistValue, FgError> {
        self.read_trailer()?;
        let top = self.top_object;
        self.parse_object(top, 0)
    }

    /// Le `size` bytes big-endian em `pos`. `None` quando `size` e invalido ou passa
    /// do buffer. Nunca indexa direto.
    fn read_uint(&self, pos: usize, size: usize) -> Option<u64> {
        if size == 0 || size > 8 {
            return None;
        }
        let end = pos.checked_add(size)?;
        let window = self.data.get(pos..end)?;
        let mut value: u64 = 0;
        for &byte in window {
            // `size <= 8`: no maximo 8 bytes sao dobrados, nenhum bit sai do u64.
            #[allow(clippy::arithmetic_side_effects)]
            {
                value = (value << 8) | u64::from(byte);
            }
        }
        Some(value)
    }

    /// Valida assinatura e trailer e carrega a tabela de offsets.
    fn read_trailer(&mut self) -> Result<(), FgError> {
        let len = self.data.len();
        if len < TRAILER_SIZE.saturating_add(SIGNATURE.len()) {
            return Err(FgError::detail(
                FgStatus::Truncated,
                format!("{len} bytes: menor que o minimo de um binary plist"),
            ));
        }
        let head = self
            .data
            .get(..SIGNATURE.len())
            .ok_or_else(|| FgError::new(FgStatus::Truncated))?;
        if head != SIGNATURE {
            let status = FgStatus::BadSignature;
            let detail = if head.starts_with(b"<?xml") {
                "plist em XML: este parser cobre apenas bplist00".to_string()
            } else if head.starts_with(b"bplist") {
                format!(
                    "versao de binary plist nao suportada (esperado bplist00, veio {})",
                    String::from_utf8_lossy(head)
                )
            } else {
                "assinatura bplist00 ausente".to_string()
            };
            return Err(FgError::detail(status, detail));
        }
        let trailer = len
            .checked_sub(TRAILER_SIZE)
            .ok_or_else(|| FgError::new(FgStatus::Truncated))?;
        let byte_at = |pos: usize| -> Result<u8, FgError> {
            self.data
                .get(pos)
                .copied()
                .ok_or_else(|| FgError::detail(FgStatus::BadTrailer, format!("trailer truncado em {pos}")))
        };
        let offset_size = usize::from(byte_at(trailer.checked_add(6).ok_or_else(|| FgError::new(FgStatus::BadTrailer))?)?);
        let ref_size = usize::from(byte_at(trailer.checked_add(7).ok_or_else(|| FgError::new(FgStatus::BadTrailer))?)?);
        if offset_size == 0 || offset_size > 8 || ref_size == 0 || ref_size > 8 {
            return Err(FgError::detail(
                FgStatus::BadTrailer,
                format!("tamanhos de inteiro invalidos no trailer (offset={offset_size}, ref={ref_size})"),
            ));
        }
        self.offset_size = offset_size;
        self.ref_size = ref_size;
        self.object_count = self.read_count(trailer.checked_add(8).ok_or_else(|| FgError::new(FgStatus::BadTrailer))?)?;
        self.top_object = self.read_count(trailer.checked_add(16).ok_or_else(|| FgError::new(FgStatus::BadTrailer))?)?;
        self.offset_table = self.read_count(trailer.checked_add(24).ok_or_else(|| FgError::new(FgStatus::BadTrailer))?)?;

        if self.object_count == 0 {
            return Err(FgError::detail(FgStatus::BadTrailer, "trailer declara zero objetos"));
        }
        if self.object_count > MAX_PLIST_NODES {
            return Err(FgError::detail(
                FgStatus::BudgetExceeded,
                format!("trailer declara {} objetos (limite {})", self.object_count, MAX_PLIST_NODES),
            ));
        }
        if self.object_count > u64::try_from(len).unwrap_or(u64::MAX) {
            return Err(FgError::detail(
                FgStatus::BadTrailer,
                format!("trailer declara {} objetos num blob de {len} bytes", self.object_count),
            ));
        }
        if self.top_object >= self.object_count {
            return Err(FgError::detail(
                FgStatus::BadTrailer,
                format!("objeto raiz {} fora da tabela de {} objetos", self.top_object, self.object_count),
            ));
        }
        let table_bytes = self
            .object_count
            .checked_mul(u64::try_from(offset_size).unwrap_or(u64::MAX))
            .ok_or_else(|| FgError::detail(FgStatus::BadOffsetTable, "tamanho da tabela com transbordo"))?;
        let limit = u64::try_from(trailer).unwrap_or(u64::MAX);
        if self.offset_table >= limit {
            return Err(FgError::detail(
                FgStatus::BadOffsetTable,
                format!("tabela de offsets comeca em {} (trailer em {limit})", self.offset_table),
            ));
        }
        let table_end = self
            .offset_table
            .checked_add(table_bytes)
            .ok_or_else(|| FgError::detail(FgStatus::BadOffsetTable, "fim da tabela com transbordo"))?;
        if table_end > u64::try_from(len).unwrap_or(u64::MAX) {
            return Err(FgError::detail(
                FgStatus::BadOffsetTable,
                format!("tabela de offsets termina em {table_end}, alem dos {len} bytes"),
            ));
        }
        let capacity = usize::try_from(self.object_count).unwrap_or(0).min(RESERVE_HINT);
        self.offsets = Vec::with_capacity(capacity);
        for index in 0..self.object_count {
            let base = u64::try_from(offset_size)
                .unwrap_or(u64::MAX)
                .checked_mul(index)
                .ok_or_else(|| FgError::detail(FgStatus::BadOffsetTable, "indice da tabela com transbordo"))?;
            let pos = self
                .offset_table
                .checked_add(base)
                .and_then(|value| usize::try_from(value).ok())
                .ok_or_else(|| FgError::detail(FgStatus::BadOffsetTable, "offset da tabela com transbordo"))?;
            let offset = self.read_uint(pos, offset_size).ok_or_else(|| {
                FgError::detail(FgStatus::BadOffsetTable, format!("offset {index} truncado"))
            })?;
            self.offsets.push(offset);
        }
        Ok(())
    }

    fn read_count(&self, pos: usize) -> Result<u64, FgError> {
        self.read_uint(pos, 8)
            .ok_or_else(|| FgError::detail(FgStatus::BadTrailer, format!("campo do trailer truncado em {pos}")))
    }

    /// Le um comprimento: `info` direto, ou objeto inteiro seguinte quando `info == 0x0F`.
    fn read_length(&self, pos: &mut usize, info: u8) -> Result<u64, FgError> {
        if info != 0x0F {
            return Ok(u64::from(info));
        }
        let marker = self
            .data
            .get(*pos)
            .copied()
            .ok_or_else(|| FgError::detail(FgStatus::Truncated, "comprimento estendido truncado"))?;
        if marker >> 4 != 0x1 {
            return Err(FgError::detail(
                FgStatus::BadLength,
                format!("comprimento estendido sem marcador de inteiro (0x{})", hex_byte(marker)),
            ));
        }
        let width = 1usize.checked_shl(u32::from(marker & 0x0F)).unwrap_or(0);
        *pos = pos
            .checked_add(1)
            .ok_or_else(|| FgError::detail(FgStatus::BadLength, "posicao com transbordo"))?;
        if width == 0 || width > 8 {
            return Err(FgError::detail(
                FgStatus::BadLength,
                format!("largura de comprimento nao suportada ({width} bytes)"),
            ));
        }
        let value = self.read_uint(*pos, width).ok_or_else(|| {
            FgError::detail(FgStatus::BadLength, "comprimento estendido fora do buffer")
        })?;
        *pos = pos
            .checked_add(width)
            .ok_or_else(|| FgError::detail(FgStatus::BadLength, "posicao com transbordo"))?;
        Ok(value)
    }

    /// Fatia `count` bytes a partir de `pos`, checando contra o buffer e cobrando o orcamento.
    fn take_slice(&mut self, pos: usize, count: u64) -> Result<&'a [u8], FgError> {
        let data = self.data;
        let count_usize = usize::try_from(count).map_err(|_| {
            FgError::detail(FgStatus::BadLength, format!("comprimento {count} nao cabe em usize"))
        })?;
        let end = pos.checked_add(count_usize).ok_or_else(|| {
            FgError::detail(FgStatus::BadLength, format!("comprimento {count} com transbordo"))
        })?;
        let window = data.get(pos..end).ok_or_else(|| {
            FgError::detail(
                FgStatus::BadLength,
                format!("{count} bytes a partir de {pos} passam do buffer de {}", data.len()),
            )
        })?;
        if !self.budget.bytes(count) {
            return Err(FgError::detail(
                FgStatus::BudgetExceeded,
                format!("payload copiado passa de {MAX_PLIST_PAYLOAD} bytes"),
            ));
        }
        Ok(window)
    }

    /// Le a referencia `index`, cobrando um no e recusando ciclo.
    fn parse_object(&mut self, index: u64, depth: u32) -> Result<PlistValue, FgError> {
        if depth > MAX_PLIST_DEPTH {
            return Err(FgError::detail(
                FgStatus::DepthLimit,
                format!("aninhamento maior que {MAX_PLIST_DEPTH}"),
            ));
        }
        if !self.budget.item() {
            return Err(FgError::detail(
                FgStatus::BudgetExceeded,
                format!("mais de {MAX_PLIST_NODES} objetos visitados"),
            ));
        }
        if index >= self.object_count {
            return Err(FgError::detail(
                FgStatus::BadReference,
                format!("referencia {index} fora da tabela de {} objetos", self.object_count),
            ));
        }
        if self.stack.contains(&index) {
            return Err(FgError::detail(
                FgStatus::Cycle,
                format!("objeto {index} referencia a si mesmo (ciclo)"),
            ));
        }
        let offset = self.offsets.get(usize::try_from(index).unwrap_or(usize::MAX)).copied().ok_or_else(|| {
            FgError::detail(FgStatus::BadReference, format!("offset do objeto {index} ausente"))
        })?;
        let pos = usize::try_from(offset).map_err(|_| {
            FgError::detail(FgStatus::BadReference, format!("offset {offset} nao cabe em usize"))
        })?;
        if pos >= self.data.len() {
            return Err(FgError::detail(
                FgStatus::BadReference,
                format!("offset {offset} do objeto {index} fora do blob de {} bytes", self.data.len()),
            ));
        }
        self.stack.push(index);
        let result = self.parse_body(pos, depth);
        let _popped = self.stack.pop();
        result
    }

    /// Le o objeto em `pos` (marcador + corpo).
    fn parse_body(&mut self, pos: usize, depth: u32) -> Result<PlistValue, FgError> {
        let marker = self
            .data
            .get(pos)
            .copied()
            .ok_or_else(|| FgError::detail(FgStatus::Truncated, format!("marcador truncado em {pos}")))?;
        let kind = marker >> 4;
        let info = marker & 0x0F;
        let body = pos
            .checked_add(1)
            .ok_or_else(|| FgError::detail(FgStatus::Truncated, "posicao do marcador com transbordo"))?;
        match kind {
            0x0 => match info {
                0x0 | 0xF => Ok(PlistValue::Null),
                0x8 => Ok(PlistValue::Bool(false)),
                0x9 => Ok(PlistValue::Bool(true)),
                _ => Err(FgError::detail(
                    FgStatus::BadMarker,
                    format!("marcador nulo invalido (0x{})", hex_byte(marker)),
                )),
            },
            0x1 => {
                let width = 1usize.checked_shl(u32::from(info)).unwrap_or(0);
                if width == 0 || width > 8 {
                    return Err(FgError::detail(
                        FgStatus::Unsupported,
                        format!("inteiro de {width} bytes nao cabe em i64"),
                    ));
                }
                let raw = self.read_uint(body, width).ok_or_else(|| {
                    FgError::detail(FgStatus::Truncated, format!("inteiro de {width} bytes truncado"))
                })?;
                // Reinterpretacao de bits, igual ao static_cast<int64_t> do C++.
                Ok(PlistValue::Integer(raw as i64))
            }
            0x2 => {
                let width = 1usize.checked_shl(u32::from(info)).unwrap_or(0);
                if width != 4 && width != 8 {
                    return Err(FgError::detail(
                        FgStatus::Unsupported,
                        format!("real de {width} bytes nao suportado"),
                    ));
                }
                let raw = self.read_uint(body, width).ok_or_else(|| {
                    FgError::detail(FgStatus::Truncated, format!("real de {width} bytes truncado"))
                })?;
                let value = if width == 4 {
                    f64::from(f32::from_bits(raw as u32))
                } else {
                    f64::from_bits(raw)
                };
                Ok(PlistValue::Real(value))
            }
            0x3 => {
                if info != 0x3 {
                    return Err(FgError::detail(
                        FgStatus::BadMarker,
                        format!("marcador de data invalido (0x{})", hex_byte(marker)),
                    ));
                }
                let raw = self.read_uint(body, 8).ok_or_else(|| {
                    FgError::detail(FgStatus::Truncated, "data de 8 bytes truncada")
                })?;
                Ok(PlistValue::Date(f64::from_bits(raw)))
            }
            0x4 => {
                let mut cursor = body;
                let count = self.read_length(&mut cursor, info)?;
                let window = self.take_slice(cursor, count)?;
                Ok(PlistValue::Data(window.to_vec()))
            }
            0x5 => {
                let mut cursor = body;
                let count = self.read_length(&mut cursor, info)?;
                let window = self.take_slice(cursor, count)?;
                Ok(PlistValue::String(window.to_vec()))
            }
            0x6 => {
                let mut cursor = body;
                let units = self.read_length(&mut cursor, info)?;
                let bytes = units.checked_mul(2).ok_or_else(|| {
                    FgError::detail(
                        FgStatus::BadLength,
                        format!("string UTF-16 com {units} unidades: comprimento com transbordo"),
                    )
                })?;
                let window = self.take_slice(cursor, bytes)?;
                let mut decoded: Vec<u16> = Vec::with_capacity(
                    usize::try_from(units).unwrap_or(0).min(RESERVE_HINT),
                );
                let mut chunks = window.chunks_exact(2);
                for pair in &mut chunks {
                    let high = pair.first().copied().unwrap_or(0);
                    let low = pair.get(1).copied().unwrap_or(0);
                    decoded.push(u16::from_be_bytes([high, low]));
                }
                if !chunks.remainder().is_empty() {
                    return Err(FgError::detail(
                        FgStatus::BadUtf16,
                        "string UTF-16 com numero impar de bytes",
                    ));
                }
                let text = String::from_utf16_lossy(&decoded);
                if !self.budget.bytes(u64::try_from(text.len()).unwrap_or(u64::MAX)) {
                    return Err(FgError::detail(
                        FgStatus::BudgetExceeded,
                        format!("payload copiado passa de {MAX_PLIST_PAYLOAD} bytes"),
                    ));
                }
                Ok(PlistValue::Utf16(text))
            }
            0x8 => {
                let width = usize::from(info)
                    .checked_add(1)
                    .ok_or_else(|| FgError::detail(FgStatus::Unsupported, "largura de uid com transbordo"))?;
                if width > 8 {
                    return Err(FgError::detail(
                        FgStatus::Unsupported,
                        format!("uid de {width} bytes nao suportado"),
                    ));
                }
                let raw = self.read_uint(body, width).ok_or_else(|| {
                    FgError::detail(FgStatus::Truncated, format!("uid de {width} bytes truncado"))
                })?;
                Ok(PlistValue::Uid(raw))
            }
            0xA..=0xC => {
                let mut cursor = body;
                let count = self.read_length(&mut cursor, info)?;
                self.check_table_fits(cursor, count, 1)?;
                let capacity = usize::try_from(count).unwrap_or(0).min(RESERVE_HINT);
                let mut items = Vec::with_capacity(capacity);
                let mut position = cursor;
                for index in 0..count {
                    let reference = self.read_uint(position, self.ref_size).ok_or_else(|| {
                        FgError::detail(
                            FgStatus::Truncated,
                            format!("referencia {index} de {count} truncada"),
                        )
                    })?;
                    position = position.checked_add(self.ref_size).ok_or_else(|| {
                        FgError::detail(FgStatus::BadLength, "posicao de referencia com transbordo")
                    })?;
                    items.push(self.parse_object(reference, depth.saturating_add(1))?);
                }
                Ok(if kind == 0xC {
                    PlistValue::Set(items)
                } else {
                    PlistValue::Array(items)
                })
            }
            0xD => {
                let mut cursor = body;
                let count = self.read_length(&mut cursor, info)?;
                self.check_table_fits(cursor, count, 2)?;
                let capacity = usize::try_from(count).unwrap_or(0).min(RESERVE_HINT);
                let mut keys: Vec<u64> = Vec::with_capacity(capacity);
                let mut position = cursor;
                for index in 0..count {
                    let reference = self.read_uint(position, self.ref_size).ok_or_else(|| {
                        FgError::detail(
                            FgStatus::Truncated,
                            format!("chave {index} de {count} truncada"),
                        )
                    })?;
                    position = position.checked_add(self.ref_size).ok_or_else(|| {
                        FgError::detail(FgStatus::BadLength, "posicao de chave com transbordo")
                    })?;
                    keys.push(reference);
                }
                let mut entries: Vec<(Vec<u8>, PlistValue)> = Vec::with_capacity(capacity);
                for reference in keys {
                    let key = self.parse_object(reference, depth.saturating_add(1))?;
                    let name = key_name(&key);
                    let value_reference = self.read_uint(position, self.ref_size).ok_or_else(|| {
                        FgError::detail(FgStatus::Truncated, "valor de dicionario truncado")
                    })?;
                    position = position.checked_add(self.ref_size).ok_or_else(|| {
                        FgError::detail(FgStatus::BadLength, "posicao de valor com transbordo")
                    })?;
                    let value = self.parse_object(value_reference, depth.saturating_add(1))?;
                    entries.push((name, value));
                }
                Ok(PlistValue::Dict(entries))
            }
            _ => Err(FgError::detail(
                FgStatus::Unsupported,
                format!("tipo de objeto binary plist nao suportado (0x{})", hex_byte(kind)),
            )),
        }
    }

    /// Garante que `count * ref_size * slots` cabe no buffer restante.
    fn check_table_fits(&self, pos: usize, count: u64, slots: u64) -> Result<(), FgError> {
        let remaining = self.data.len().saturating_sub(pos);
        let per_entry = u64::try_from(self.ref_size)
            .unwrap_or(u64::MAX)
            .checked_mul(slots)
            .ok_or_else(|| FgError::detail(FgStatus::BadLength, "tamanho de entrada com transbordo"))?;
        let needed = count.checked_mul(per_entry).ok_or_else(|| {
            FgError::detail(FgStatus::BadLength, format!("{count} entradas com transbordo"))
        })?;
        if needed > u64::try_from(remaining).unwrap_or(u64::MAX) {
            return Err(FgError::detail(
                FgStatus::BadLength,
                format!("{count} entradas de {per_entry} bytes passam dos {remaining} bytes restantes"),
            ));
        }
        Ok(())
    }
}

/// Nome de uma chave de dict: string vira texto, data vira hex, o resto vira
/// `<chave TIPO>` — exatamente a regra do parser C++ de referencia.
fn key_name(key: &PlistValue) -> Vec<u8> {
    match key {
        PlistValue::String(bytes) => bytes.clone(),
        PlistValue::Utf16(text) => text.as_bytes().to_vec(),
        PlistValue::Data(bytes) => hex_bytes(bytes).into_bytes(),
        other => format!("<chave {}>", other.kind_name()).into_bytes(),
    }
}

/// Hexadecimal minusculo, igual ao `bytes_to_hex` do C++.
#[must_use]
pub fn hex_bytes(bytes: &[u8]) -> String {
    const DIGITS: [u8; 16] = *b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len().saturating_mul(2));
    for &byte in bytes {
        out.push(char::from(DIGITS.get(usize::from(byte >> 4)).copied().unwrap_or(b'?')));
        out.push(char::from(DIGITS.get(usize::from(byte & 0x0F)).copied().unwrap_or(b'?')));
    }
    out
}
