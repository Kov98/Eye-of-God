//! Limites explicitos. Nao sao configuraveis em tempo de execucao de proposito:
//! um limite que o chamador pode afrouxar nao protege a estacao do perito.

/// Profundidade maxima de aninhamento do binary plist.
///
/// Igual ao `kMaxPlistDepth` de `ios_backup.cpp` (64), para que o parser Rust e o
/// parser C++ aceitem e recusem exatamente a mesma arvore.
pub const MAX_PLIST_DEPTH: u32 = 64;

/// Numero maximo de objetos que o trailer pode declarar (e que podem ser
/// visitados). Um backup real tem dezenas de milhares.
pub const MAX_PLIST_NODES: u64 = 1 << 20;

/// Total de bytes de payload (data, string, UTF-8 decodificado) que o parser do
/// plist pode copiar. Um objeto referenciado N vezes e copiado N vezes, entao o
/// orcamento e sobre o total copiado, nao sobre o tamanho do arquivo.
pub const MAX_PLIST_PAYLOAD: u64 = 64 << 20;

/// Profundidade maxima de grupos protobuf aninhados.
pub const MAX_PROTO_DEPTH: u32 = 64;

/// Numero maximo de campos protobuf visitados (todos os niveis).
pub const MAX_PROTO_FIELDS: u64 = 1 << 20;

/// Total de bytes de payload copiado do protobuf.
pub const MAX_PROTO_PAYLOAD: u64 = 64 << 20;

/// Tamanho maximo da saida JSON, em bytes.
pub const MAX_JSON_BYTES: u64 = 256 << 20;

/// Maior entrada que a ABI aceita (evita `isize` overflow ao virar slice).
pub const MAX_INPUT_BYTES: u64 = 1 << 32;

/// Quanto um vetor reserva de antemao. O resto cresce por `push`, para que uma
/// contagem declarada num arquivo hostil nao vire alocacao gigante.
pub const RESERVE_HINT: usize = 1024;

/// Orcamento de uma execucao de parsing: nos/itens e bytes copiados.
#[derive(Debug)]
pub(crate) struct Budget {
    items: u64,
    max_items: u64,
    bytes: u64,
    max_bytes: u64,
}

impl Budget {
    /// Orcamento novo, zerado.
    pub(crate) fn new(max_items: u64, max_bytes: u64) -> Self {
        Self { items: 0, max_items, bytes: 0, max_bytes }
    }

    /// Consome um no. Devolve `false` quando estoura.
    pub(crate) fn item(&mut self) -> bool {
        let Some(next) = self.items.checked_add(1) else { return false };
        self.items = next;
        next <= self.max_items
    }

    /// Consome `count` bytes de payload. Devolve `false` quando estoura.
    pub(crate) fn bytes(&mut self, count: u64) -> bool {
        let Some(next) = self.bytes.checked_add(count) else { return false };
        self.bytes = next;
        next <= self.max_bytes
    }
}
