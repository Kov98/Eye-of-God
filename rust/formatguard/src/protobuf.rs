#![forbid(unsafe_code)]
//! Wire format protobuf (o formato dos blobs do `msgstore` do `WhatsApp`) para entrada
//! nao confiavel.
//!
//! O parser trabalha no nivel do wire format: ele **nao** advinha schema. Um campo
//! length-delimited continua sendo bytes, mesmo que pareca uma mensagem aninhada — quem
//! conhece o schema chama [`parse`] de novo no payload. Isso evita a heuristica
//! "tenta parsear como mensagem" que transforma dado ambiguo em arvore inventada.
//! Aninhamento explicito no wire format (grupos, wire type 3/4) e parseado aqui.
//!
//! Cobertura de wire types:
//!
//! | wire | tipo | variante |
//! |---|---|---|
//! | 0 | varint | [`ProtoValue::Varint`] (com [`ProtoValue::zigzag`]) |
//! | 1 | fixed64 (little-endian) | [`ProtoValue::Fixed64`] |
//! | 2 | length-delimited | [`ProtoValue::Bytes`] |
//! | 3 | start group | [`ProtoValue::Group`] |
//! | 4 | end group | fecha o grupo aberto (nao vira campo) |
//! | 5 | fixed32 (little-endian) | [`ProtoValue::Fixed32`] |
//!
//! Wire types 6 e 7 sao recusados ([`FgStatus::BadWireType`]). Varint com mais de 10
//! bytes ou terminando no fim do buffer e recusado ([`FgStatus::BadVarint`]). Numero de
//! campo 0 ou maior que `2^29-1` e recusado ([`FgStatus::BadFieldNumber`]).

use crate::error::FgError;
use crate::error::FgStatus;
use crate::limits::{
    Budget, MAX_PROTO_DEPTH, MAX_PROTO_FIELDS, MAX_PROTO_PAYLOAD, RESERVE_HINT,
};

/// Maior numero de campo permitido pelo formato.
pub const MAX_FIELD_NUMBER: u64 = (1 << 29) - 1;

/// Tipo de campo no wire format.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum WireType {
    /// wire type 0.
    Varint = 0,
    /// wire type 1.
    Fixed64 = 1,
    /// wire type 2.
    LengthDelimited = 2,
    /// wire type 3.
    StartGroup = 3,
    /// wire type 4.
    EndGroup = 4,
    /// wire type 5.
    Fixed32 = 5,
}

impl WireType {
    /// Converte o nibble do wire format.
    #[must_use]
    pub fn from_u8(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Varint),
            1 => Some(Self::Fixed64),
            2 => Some(Self::LengthDelimited),
            3 => Some(Self::StartGroup),
            4 => Some(Self::EndGroup),
            5 => Some(Self::Fixed32),
            _ => None,
        }
    }

    /// Valor numerico do wire type.
    #[must_use]
    pub fn as_u8(self) -> u8 {
        self as u8
    }
}

/// Valor de um campo.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ProtoValue {
    /// wire type 0: inteiro sem sinal de 64 bits.
    Varint(u64),
    /// wire type 1: 8 bytes little-endian.
    Fixed64(u64),
    /// wire type 5: 4 bytes little-endian.
    Fixed32(u32),
    /// wire type 2: bytes exatamente como vieram.
    Bytes(Vec<u8>),
    /// wire types 3/4: grupo aninhado.
    Group(ProtoMessage),
}

impl ProtoValue {
    /// Interpretacao zigzag de um varint (`sint32`/`sint64`), quando for varint.
    ///
    /// ```
    /// # use formatguard::protobuf::{zigzag_decode, ProtoValue};
    /// assert_eq!(zigzag_decode(0), 0);
    /// assert_eq!(zigzag_decode(1), -1);
    /// assert_eq!(zigzag_decode(2), 1);
    /// assert_eq!(ProtoValue::Varint(3).zigzag(), Some(-2));
    /// ```
    #[must_use]
    pub fn zigzag(&self) -> Option<i64> {
        match self {
            Self::Varint(raw) => Some(zigzag_decode(*raw)),
            _ => None,
        }
    }
}

/// Um campo: numero, wire type e valor.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProtoField {
    /// Numero do campo (`1..=2^29-1`).
    pub number: u32,
    /// Wire type como veio no arquivo.
    pub wire: WireType,
    /// Valor decodificado.
    pub value: ProtoValue,
}

/// Uma mensagem: campos na ordem em que aparecem no buffer.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ProtoMessage {
    /// Campos da mensagem, na ordem do arquivo (repetidos preservados).
    pub fields: Vec<ProtoField>,
}

impl ProtoMessage {
    /// Quantidade de campos.
    #[must_use]
    pub fn len(&self) -> usize {
        self.fields.len()
    }

    /// Verdadeiro quando a mensagem nao tem campos.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.fields.is_empty()
    }

    /// Primeiro campo com este numero.
    #[must_use]
    pub fn first(&self, number: u32) -> Option<&ProtoField> {
        self.fields.iter().find(|field| field.number == number)
    }
}

/// Decodifica zigzag: `0 -> 0`, `1 -> -1`, `2 -> 1`, `3 -> -2`, ...
#[must_use]
pub fn zigzag_decode(value: u64) -> i64 {
    let half = (value >> 1) as i64;
    let low = (value & 1) as i64;
    half ^ low.wrapping_neg()
}

/// Parseia um buffer protobuf inteiro como uma mensagem.
///
/// Buffer vazio e uma mensagem valida sem campos.
///
/// # Erros
///
/// [`FgError`] com codigo estavel para qualquer entrada invalida.
pub fn parse(data: &[u8]) -> Result<ProtoMessage, FgError> {
    Parser::new(data).parse_message(0, None)
}

struct Parser<'a> {
    data: &'a [u8],
    pos: usize,
    budget: Budget,
}

impl<'a> Parser<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self {
            data,
            pos: 0,
            budget: Budget::new(MAX_PROTO_FIELDS, MAX_PROTO_PAYLOAD),
        }
    }

    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.pos)
    }

    fn read_byte(&mut self) -> Result<u8, FgError> {
        let byte = self.data.get(self.pos).copied().ok_or_else(|| {
            FgError::detail(
                FgStatus::Truncated,
                format!("fim do buffer em {} ({} bytes)", self.pos, self.data.len()),
            )
        })?;
        self.pos = self.pos.saturating_add(1);
        Ok(byte)
    }

    /// Le um varint de no maximo 10 bytes.
    fn read_varint(&mut self) -> Result<u64, FgError> {
        let mut value: u64 = 0;
        let mut index: u32 = 0;
        while index < 10 {
            let byte = self.read_byte().map_err(|_| {
                FgError::detail(FgStatus::BadVarint, format!("varint truncado em {}", self.pos))
            })?;
            let shift = index.saturating_mul(7);
            // Shift de 0..=63; o decimo byte contribui so com o bit 63, igual ao
            // protobuf de referencia (bits acima sao descartados).
            #[allow(clippy::arithmetic_side_effects)]
            {
                value |= u64::from(byte & 0x7F) << shift;
            }
            if byte & 0x80 == 0 {
                return Ok(value);
            }
            index = index.saturating_add(1);
        }
        Err(FgError::detail(
            FgStatus::BadVarint,
            "varint com mais de 10 bytes".to_string(),
        ))
    }

    /// Le `size` bytes little-endian em `pos`.
    fn read_fixed(&mut self, size: usize) -> Result<u64, FgError> {
        let end = self.pos.checked_add(size).ok_or_else(|| {
            FgError::detail(FgStatus::Truncated, "posicao de fixed com transbordo")
        })?;
        let window = self.data.get(self.pos..end).ok_or_else(|| {
            FgError::detail(
                FgStatus::Truncated,
                format!("fixed de {size} bytes truncado em {}", self.pos),
            )
        })?;
        let mut value: u64 = 0;
        for (index, &byte) in window.iter().enumerate() {
            let shift = u32::try_from(index).unwrap_or(0).saturating_mul(8);
            #[allow(clippy::arithmetic_side_effects)]
            {
                value |= u64::from(byte) << shift;
            }
        }
        self.pos = end;
        Ok(value)
    }

    /// Le um campo length-delimited, checando o comprimento contra o buffer restante.
    fn read_delimited(&mut self) -> Result<Vec<u8>, FgError> {
        let declared = self.read_varint()?;
        let length = usize::try_from(declared).map_err(|_| {
            FgError::detail(
                FgStatus::BadLength,
                format!("comprimento {declared} nao cabe em usize"),
            )
        })?;
        if length > self.remaining() {
            return Err(FgError::detail(
                FgStatus::BadLength,
                format!("comprimento {length} maior que os {} bytes restantes", self.remaining()),
            ));
        }
        if !self.budget.bytes(declared) {
            return Err(FgError::detail(
                FgStatus::BudgetExceeded,
                format!("payload copiado passa de {MAX_PROTO_PAYLOAD} bytes"),
            ));
        }
        let end = self.pos.checked_add(length).ok_or_else(|| {
            FgError::detail(FgStatus::BadLength, "fim do campo com transbordo")
        })?;
        let window = self.data.get(self.pos..end).ok_or_else(|| {
            FgError::detail(FgStatus::BadLength, "campo length-delimited fora do buffer")
        })?;
        self.pos = end;
        Ok(window.to_vec())
    }

    /// Parseia campos ate o fim do buffer (grupo aberto: ate o `END_GROUP` correspondente).
    fn parse_message(
        &mut self,
        depth: u32,
        open_group: Option<u32>,
    ) -> Result<ProtoMessage, FgError> {
        if depth > MAX_PROTO_DEPTH {
            return Err(FgError::detail(
                FgStatus::DepthLimit,
                format!("grupos aninhados alem de {MAX_PROTO_DEPTH}"),
            ));
        }
        let mut fields: Vec<ProtoField> = Vec::with_capacity(RESERVE_HINT.min(16));
        loop {
            if self.remaining() == 0 {
                if open_group.is_some() {
                    return Err(FgError::detail(
                        FgStatus::BadGroup,
                        "grupo sem END_GROUP antes do fim do buffer",
                    ));
                }
                return Ok(ProtoMessage { fields });
            }
            if !self.budget.item() {
                return Err(FgError::detail(
                    FgStatus::BudgetExceeded,
                    format!("mais de {MAX_PROTO_FIELDS} campos"),
                ));
            }
            let key_pos = self.pos;
            let key = self.read_varint()?;
            let number_raw = key >> 3;
            if number_raw == 0 || number_raw > MAX_FIELD_NUMBER {
                return Err(FgError::detail(
                    FgStatus::BadFieldNumber,
                    format!("numero de campo {number_raw} em {key_pos}"),
                ));
            }
            let number = u32::try_from(number_raw).map_err(|_| {
                FgError::detail(FgStatus::BadFieldNumber, format!("campo {number_raw} nao cabe em u32"))
            })?;
            let wire_raw = (key & 0x07) as u8;
            let wire = WireType::from_u8(wire_raw).ok_or_else(|| {
                FgError::detail(
                    FgStatus::BadWireType,
                    format!("wire type {wire_raw} no campo {number}"),
                )
            })?;
            match wire {
                WireType::Varint => {
                    let raw = self.read_varint()?;
                    fields.push(ProtoField { number, wire, value: ProtoValue::Varint(raw) });
                }
                WireType::Fixed64 => {
                    let raw = self.read_fixed(8)?;
                    fields.push(ProtoField { number, wire, value: ProtoValue::Fixed64(raw) });
                }
                WireType::Fixed32 => {
                    let raw = self.read_fixed(4)?;
                    let truncated = u32::try_from(raw).unwrap_or(0);
                    fields.push(ProtoField { number, wire, value: ProtoValue::Fixed32(truncated) });
                }
                WireType::LengthDelimited => {
                    let bytes = self.read_delimited()?;
                    fields.push(ProtoField { number, wire, value: ProtoValue::Bytes(bytes) });
                }
                WireType::StartGroup => {
                    let inner = self.parse_message(depth.saturating_add(1), Some(number))?;
                    fields.push(ProtoField { number, wire, value: ProtoValue::Group(inner) });
                }
                WireType::EndGroup => match open_group {
                    Some(expected) if expected == number => {
                        return Ok(ProtoMessage { fields });
                    }
                    Some(expected) => {
                        return Err(FgError::detail(
                            FgStatus::BadGroup,
                            format!("END_GROUP do campo {number} fecha um grupo aberto pelo campo {expected}"),
                        ));
                    }
                    None => {
                        return Err(FgError::detail(
                            FgStatus::BadGroup,
                            format!("END_GROUP do campo {number} sem grupo aberto"),
                        ));
                    }
                },
            }
        }
    }
}
