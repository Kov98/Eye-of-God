//! `formatguard` — parsers Rust para os dois formatos onde a entrada e hostil por
//! definicao num aparelho apreendido: o `msgstore` do `WhatsApp` (protobuf) e um backup
//! iOS (`bplist00`).
//!
//! O objetivo nao e reescrever o produto: e dar ao C++ uma camada que nunca derruba a
//! estacao do perito. Regras que valem para todo este crate:
//!
//! * nenhuma entrada do usuario e confiada: todo comprimento e checado contra o buffer,
//!   todo indice e checado contra a tabela, toda aritmetica e `checked_*`;
//! * zero panic alcancavel: os modulos de parsing sao `#![forbid(unsafe_code)]` e o
//!   crate nega (`deny`) `unwrap_used`, `expect_used`, `panic`, `indexing_slicing`,
//!   `arithmetic_side_effects` e afins;
//! * limites explicitos de profundidade, de numero de nos e de bytes copiados
//!   ([LIMITES](`limits`));
//! * `unsafe` existe em exatamente um modulo (`abi`), porque a ABI C precisa de
//!   ponteiros crus.
//!
//! # Camadas
//!
//! * [`plist`] — Apple binary plist `bplist00`: null, bool, int (1/2/4/8/16 bytes),
//!   real (float32/float64), date, data, string ASCII, string UTF-16BE, uid, array,
//!   set e dict. Tabela de offsets validada, referencias circulares detectadas.
//! * [`protobuf`] — wire format protobuf: varint (com truncamento), zigzag, fixed32,
//!   fixed64, length-delimited e grupos (start/end group), com profundidade limitada.
//! * [`json`] — serializacao canonica e deterministica das duas arvores.
//! * [`abi`] — a ABI C estreita documentada em `include/formatguard.h`.
//!
//! # Exemplo
//!
//! ```
//! let blob = b"nao e um plist";
//! let erro = formatguard::plist::parse(blob).unwrap_err();
//! assert!(erro.status().as_i32() < 0);
//! ```

#![deny(unsafe_code)]
#![warn(missing_docs)]
#![warn(clippy::pedantic)]
#![deny(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing,
    clippy::arithmetic_side_effects,
    clippy::integer_division,
    clippy::unreachable,
    clippy::todo,
    clippy::unimplemented,
    clippy::dbg_macro,
    clippy::exit,
    clippy::panic_in_result_fn,
    clippy::let_underscore_must_use,
    clippy::string_slice
)]
#![allow(
    // Os nomes seguem o formato (bplist00, protobuf) e a ABI C, nao o guia de estilo Rust.
    clippy::module_name_repetitions,
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::cast_precision_loss,
    clippy::cast_lossless,
    clippy::missing_errors_doc,
    clippy::must_use_candidate
)]

pub mod abi;
pub mod error;
pub mod json;
pub mod limits;
pub mod plist;
pub mod protobuf;

pub use error::{FgError, FgStatus};
