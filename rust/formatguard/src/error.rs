//! Codigos de erro estaveis e o tipo de erro interno.

use std::fmt;

/// Codigos devolvidos pela ABI C. Os valores numericos **nao mudam**: o C++ do
/// produto pode compara-los com `==` contra as constantes do header.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FgStatus {
    /// Sucesso.
    Ok = 0,
    /// Ponteiro de entrada ou de saida nulo.
    NullArgument = -1,
    /// Entrada vazia (um plist nao pode ter zero bytes).
    EmptyInput = -2,
    /// `len` maior do que qualquer buffer valido.
    InputTooLarge = -3,
    /// Assinatura ausente ou versao de plist nao suportada.
    BadSignature = -4,
    /// Buffer terminou no meio de um campo.
    Truncated = -5,
    /// Trailer do `bplist00` inconsistente.
    BadTrailer = -6,
    /// Tabela de offsets fora do buffer ou incoerente.
    BadOffsetTable = -7,
    /// Marcador de tipo desconhecido ou invalido.
    BadMarker = -8,
    /// Comprimento declarado maior que o buffer restante (ou com transbordo).
    BadLength = -9,
    /// Aninhamento acima do limite.
    DepthLimit = -10,
    /// Referencia circular entre objetos.
    Cycle = -11,
    /// Orcamento de nos/bytes estourado.
    BudgetExceeded = -12,
    /// Referencia a objeto inexistente.
    BadReference = -13,
    /// Tipo ou largura nao suportada.
    Unsupported = -14,
    /// Sequencia UTF-16 invalida.
    BadUtf16 = -15,
    /// Varint invalido ou truncado.
    BadVarint = -16,
    /// Wire type protobuf desconhecido (6 ou 7).
    BadWireType = -17,
    /// Numero de campo protobuf fora de `1..=2^29-1`.
    BadFieldNumber = -18,
    /// Grupo protobuf mal formado (sem `END_GROUP`, ou numero divergente).
    BadGroup = -19,
    /// Saida JSON maior que o limite configurado.
    JsonLimit = -20,
    /// Falha interna (panic capturado na fronteira C). Nunca deveria acontecer.
    Internal = -99,
}

impl FgStatus {
    /// Valor numerico usado na ABI.
    #[must_use]
    pub fn as_i32(self) -> i32 {
        self as i32
    }

    /// Mensagem fixa em portugues, sem alocacao.
    #[must_use]
    pub fn message(self) -> &'static str {
        match self {
            Self::Ok => "ok",
            Self::NullArgument => "ponteiro nulo recusado",
            Self::EmptyInput => "entrada vazia",
            Self::InputTooLarge => "entrada maior que o limite da ABI",
            Self::BadSignature => "assinatura bplist00 ausente ou versao nao suportada",
            Self::Truncated => "buffer terminou no meio de um campo",
            Self::BadTrailer => "trailer do binary plist invalido",
            Self::BadOffsetTable => "tabela de offsets invalida",
            Self::BadMarker => "marcador de tipo invalido",
            Self::BadLength => "comprimento declarado maior que o buffer",
            Self::DepthLimit => "aninhamento acima do limite",
            Self::Cycle => "referencia circular entre objetos",
            Self::BudgetExceeded => "orcamento de nos ou bytes estourado",
            Self::BadReference => "referencia a objeto inexistente",
            Self::Unsupported => "tipo ou largura nao suportada",
            Self::BadUtf16 => "sequencia UTF-16 invalida",
            Self::BadVarint => "varint invalido ou truncado",
            Self::BadWireType => "wire type protobuf desconhecido",
            Self::BadFieldNumber => "numero de campo protobuf invalido",
            Self::BadGroup => "grupo protobuf mal formado",
            Self::JsonLimit => "saida JSON maior que o limite",
            Self::Internal => "falha interna",
        }
    }

    /// Converte o codigo numerico de volta para o enum (para testes e para a ABI).
    #[must_use]
    pub fn from_i32(code: i32) -> Option<Self> {
        const ALL: [FgStatus; 22] = [
            FgStatus::Ok,
            FgStatus::NullArgument,
            FgStatus::EmptyInput,
            FgStatus::InputTooLarge,
            FgStatus::BadSignature,
            FgStatus::Truncated,
            FgStatus::BadTrailer,
            FgStatus::BadOffsetTable,
            FgStatus::BadMarker,
            FgStatus::BadLength,
            FgStatus::DepthLimit,
            FgStatus::Cycle,
            FgStatus::BudgetExceeded,
            FgStatus::BadReference,
            FgStatus::Unsupported,
            FgStatus::BadUtf16,
            FgStatus::BadVarint,
            FgStatus::BadWireType,
            FgStatus::BadFieldNumber,
            FgStatus::BadGroup,
            FgStatus::JsonLimit,
            FgStatus::Internal,
        ];
        ALL.iter().copied().find(|status| status.as_i32() == code)
    }
}

impl fmt::Display for FgStatus {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message())
    }
}

/// Erro de parsing: codigo estavel mais um detalhe textual para diagnostico.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FgError {
    status: FgStatus,
    detail: String,
}

impl FgError {
    /// Erro com detalhe textual livre.
    #[must_use]
    pub fn new(status: FgStatus) -> Self {
        Self { status, detail: String::new() }
    }

    /// Erro com detalhe (usado para dizer qual byte/offset recusou).
    #[must_use]
    pub fn detail(status: FgStatus, detail: impl Into<String>) -> Self {
        Self { status, detail: detail.into() }
    }

    /// Codigo estavel.
    #[must_use]
    pub fn status(&self) -> FgStatus {
        self.status
    }

    /// Detalhe textual (pode ser vazio).
    #[must_use]
    pub fn detail_text(&self) -> &str {
        &self.detail
    }

    /// Texto completo: `status: detalhe`.
    #[must_use]
    pub fn message(&self) -> String {
        if self.detail.is_empty() {
            self.status.message().to_string()
        } else {
            format!("{}: {}", self.status.message(), self.detail)
        }
    }
}

impl fmt::Display for FgError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message())
    }
}

impl std::error::Error for FgError {}

/// `0xAB` em hexadecimal minusculo, para detalhes de erro.
pub(crate) fn hex_byte(byte: u8) -> String {
    const DIGITS: [u8; 16] = *b"0123456789abcdef";
    let high = DIGITS.get(usize::from(byte >> 4)).copied().unwrap_or(b'?');
    let low = DIGITS.get(usize::from(byte & 0x0F)).copied().unwrap_or(b'?');
    String::from_utf8_lossy(&[high, low]).into_owned()
}
