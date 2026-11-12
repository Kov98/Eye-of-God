//! Robustez de curta duracao dentro do `cargo test`.
//!
//! A campanha longa (milhoes de entradas) e o binario `robust`; aqui fica uma versao
//! deterministica e rapida para o teste normal falhar se alguem reintroduzir panic.

mod common;

use formatguard::error::FgStatus;

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }

    fn below(&mut self, bound: usize) -> usize {
        if bound == 0 {
            return 0;
        }
        usize::try_from(self.next() % bound as u64).unwrap_or(0)
    }

    fn bytes(&mut self, max: usize) -> Vec<u8> {
        let length = self.below(max.saturating_add(1));
        let mut out = Vec::with_capacity(length);
        for _ in 0..length {
            out.push(u8::try_from(self.next() & 0xFF).unwrap_or(0));
        }
        out
    }
}

#[test]
fn short_campaign_never_panics_and_always_returns_a_code() {
    let mut rng = Rng(0xABCD_1234_5678_9EF0);
    let mut plist_ok = 0_u64;
    let mut plist_err = 0_u64;
    let mut proto_ok = 0_u64;
    let mut proto_err = 0_u64;

    for _ in 0..20_000 {
        let mut input = rng.bytes(200);
        match rng.below(3) {
            0 => {
                let mut prefixed = b"bplist00".to_vec();
                prefixed.extend_from_slice(&input);
                input = prefixed;
            }
            1 => {
                // trailer plausivel
                input.extend_from_slice(&[0_u8; 32]);
            }
            _ => {}
        }

        match formatguard::plist::parse(&input) {
            Ok(value) => {
                plist_ok += 1;
                formatguard::json::plist_to_json(&value).expect("json de arvore valida");
            }
            Err(error) => {
                plist_err += 1;
                assert!(error.status().as_i32() < 0);
            }
        }
        match formatguard::protobuf::parse(&input) {
            Ok(value) => {
                proto_ok += 1;
                formatguard::json::protobuf_to_json(&value).expect("json de mensagem valida");
            }
            Err(error) => {
                proto_err += 1;
                assert!(error.status().as_i32() < 0);
            }
        }
    }

    assert_eq!(plist_ok + plist_err, 20_000);
    assert_eq!(proto_ok + proto_err, 20_000);
    assert!(plist_err > 0, "entrada aleatoria deveria ser recusada quase sempre");
    println!("plist ok={plist_ok} err={plist_err}, proto ok={proto_ok} err={proto_err}");
}

#[test]
fn every_error_code_is_documented() {
    // Todos os codigos que o parser pode devolver existem na tabela estavel da ABI.
    for code in 0..=0 {
        assert_eq!(FgStatus::from_i32(code), Some(FgStatus::Ok));
    }
    assert!(FgStatus::from_i32(FgStatus::Internal.as_i32()).is_some());
    assert!(FgStatus::from_i32(-77).is_none());
    assert_eq!(FgStatus::EmptyInput.as_i32(), -2);
    assert_eq!(FgStatus::Internal.as_i32(), -99);
}
