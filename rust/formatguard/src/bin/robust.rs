//! `robust` — campanha de robustez: milhoes de entradas aleatorias e mutacoes
//! estruturadas do corpus, contando exatamente o que aconteceu.
//!
//! Uso:
//!   robust [--iters N] [--seed S] [--corpus DIR]...
//!
//! Cada iteracao chama os dois parsers (plist e protobuf) dentro de `catch_unwind`, para
//! que um panic seja **contado** em vez de matar a medicao. Periodicamente a mesma
//! entrada passa pela ABI C (parse + to_json + free) para exercitar a fronteira unsafe.
//!
//! Codigo de saida: 0 = nenhum panic, 1 = algum panic (o que reprova a campanha).

use std::panic::AssertUnwindSafe;
use std::process::ExitCode;
use std::time::Instant;

struct Rng {
    state: u64,
}

impl Rng {
    fn new(seed: u64) -> Self {
        Self { state: seed | 1 }
    }

    fn next(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    fn below(&mut self, bound: usize) -> usize {
        if bound == 0 {
            return 0;
        }
        usize::try_from(self.next() % u64::try_from(bound).unwrap_or(1)).unwrap_or(0)
    }

    fn byte(&mut self) -> u8 {
        u8::try_from(self.next() & 0xFF).unwrap_or(0)
    }
}

#[derive(Default)]
struct Counters {
    inputs: u64,
    plist_ok: u64,
    plist_err: u64,
    proto_ok: u64,
    proto_err: u64,
    panics: u64,
    abi_calls: u64,
    abi_failures: u64,
    bytes: u64,
}

fn random_bytes(rng: &mut Rng, max_len: usize) -> Vec<u8> {
    let length = rng.below(max_len.saturating_add(1));
    let mut data = Vec::with_capacity(length);
    for _ in 0..length {
        data.push(rng.byte());
    }
    data
}

fn plist_like(rng: &mut Rng) -> Vec<u8> {
    let mut data = b"bplist00".to_vec();
    let tail = random_bytes(rng, 200);
    data.extend_from_slice(&tail);
    data
}

/// Monta um plist com trailer plausivel mas campos aleatorios, para exercitar a
/// validacao do trailer e da tabela de offsets.
fn plist_trailer_fuzz(rng: &mut Rng) -> Vec<u8> {
    let mut data = b"bplist00".to_vec();
    let objects = random_bytes(rng, 24);
    data.extend_from_slice(&objects);
    let mut trailer = [0_u8; 32];
    for slot in trailer.iter_mut() {
        *slot = rng.byte();
    }
    // Metade das vezes, um trailer quase valido.
    if rng.below(2) == 0 {
        trailer[6] = u8::try_from(1 + rng.below(8)).unwrap_or(1);
        trailer[7] = u8::try_from(1 + rng.below(8)).unwrap_or(1);
        trailer[8..16].copy_from_slice(&1_u64.to_be_bytes());
        trailer[16..24].copy_from_slice(&0_u64.to_be_bytes());
        let table = u64::try_from(data.len()).unwrap_or(0);
        trailer[24..32].copy_from_slice(&table.to_be_bytes());
    }
    data.extend_from_slice(&trailer);
    data
}

fn mutate(rng: &mut Rng, seed: &[u8]) -> Vec<u8> {
    let mut data = seed.to_vec();
    let rounds = 1 + rng.below(8);
    for _ in 0..rounds {
        match rng.below(7) {
            0 => {
                if !data.is_empty() {
                    let position = rng.below(data.len());
                    data[position] ^= 1 << rng.below(8);
                }
            }
            1 => {
                if !data.is_empty() {
                    let position = rng.below(data.len());
                    data[position] = rng.byte();
                }
            }
            2 => {
                if !data.is_empty() {
                    let position = rng.below(data.len());
                    let length = 1 + rng.below(16);
                    let end = position.saturating_add(length).min(data.len());
                    data.drain(position..end);
                }
            }
            3 => {
                let position = rng.below(data.len().saturating_add(1));
                let extra = random_bytes(rng, 8);
                for (offset, byte) in extra.iter().enumerate() {
                    data.insert(position.saturating_add(offset).min(data.len()), *byte);
                }
            }
            4 => {
                let keep = rng.below(data.len().saturating_add(1));
                data.truncate(keep);
            }
            5 => {
                if !data.is_empty() {
                    let position = rng.below(data.len());
                    let end = position.saturating_add(1 + rng.below(32)).min(data.len());
                    for slot in data.get_mut(position..end).unwrap_or(&mut []) {
                        *slot = 0;
                    }
                }
            }
            _ => {
                if data.len() < 4096 {
                    let extra = random_bytes(rng, 16);
                    data.extend_from_slice(&extra);
                }
            }
        }
    }
    data
}

/// Buffer que parece protobuf: cadeia de chaves varint e payloads de tamanho sorteado.
fn proto_like(rng: &mut Rng) -> Vec<u8> {
    let mut data = Vec::new();
    let fields = rng.below(8);
    for _ in 0..fields {
        let number = 1 + rng.below(200);
        let wire = rng.below(6);
        let key = (u64::try_from(number).unwrap_or(1) << 3) | u64::try_from(wire).unwrap_or(0);
        write_varint(&mut data, key);
        match wire {
            0 => write_varint(&mut data, rng.next()),
            1 => data.extend_from_slice(&rng.next().to_le_bytes()),
            2 => {
                let length = rng.below(32);
                write_varint(&mut data, u64::try_from(length).unwrap_or(0));
                for _ in 0..length {
                    data.push(rng.byte());
                }
            }
            5 => {
                let value = u32::try_from(rng.next() & 0xFFFF_FFFF).unwrap_or(0);
                data.extend_from_slice(&value.to_le_bytes());
            }
            _ => {}
        }
    }
    data
}

fn write_varint(out: &mut Vec<u8>, mut value: u64) {
    loop {
        let mut byte = u8::try_from(value & 0x7F).unwrap_or(0);
        value >>= 7;
        if value != 0 {
            byte |= 0x80;
        }
        out.push(byte);
        if value == 0 {
            break;
        }
    }
}

fn load_corpus(dirs: &[String]) -> Vec<Vec<u8>> {
    let mut seeds: Vec<Vec<u8>> = Vec::new();
    for dir in dirs {
        let mut stack = vec![std::path::PathBuf::from(dir)];
        while let Some(path) = stack.pop() {
            let Ok(entries) = std::fs::read_dir(&path) else { continue };
            for entry in entries.flatten() {
                let child = entry.path();
                match entry.file_type() {
                    Ok(kind) if kind.is_dir() => stack.push(child),
                    Ok(kind) if kind.is_file() => {
                        let size = entry.metadata().map(|meta| meta.len()).unwrap_or(0);
                        if size > 0 && size <= 1 << 20 {
                            if let Ok(data) = std::fs::read(&child) {
                                seeds.push(data);
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }
    seeds
}

fn abi_roundtrip(input: &[u8], counters: &mut Counters) {
    use formatguard::abi::{
        fg_free_plist, fg_free_proto, fg_free_string, fg_parse_plist, fg_parse_protobuf,
        fg_plist_to_json, fg_proto_to_json,
    };
    counters.abi_calls += 1;

    // SAFETY: cada chamada usa um buffer vivo (`input`) e handles que so sao liberados
    // uma vez, nesta mesma funcao. As funcoes recusam ponteiro nulo sem dereferenciar.
    let mut plist_handle: *mut formatguard::abi::fg_plist = std::ptr::null_mut();
    let status = unsafe { fg_parse_plist(input.as_ptr(), input.len(), &mut plist_handle) };
    if status == 0 && !plist_handle.is_null() {
        let mut json: *mut std::os::raw::c_char = std::ptr::null_mut();
        let mut json_len: usize = 0;
        let status = unsafe { fg_plist_to_json(plist_handle, &mut json, &mut json_len) };
        if status != 0 || json.is_null() || json_len == 0 {
            counters.abi_failures += 1;
        }
        unsafe { fg_free_string(json) };
    } else if status != 0 && !plist_handle.is_null() {
        counters.abi_failures += 1;
    }
    unsafe { fg_free_plist(plist_handle) };

    let mut proto_handle: *mut formatguard::abi::fg_proto = std::ptr::null_mut();
    let status = unsafe { fg_parse_protobuf(input.as_ptr(), input.len(), &mut proto_handle) };
    if status == 0 && !proto_handle.is_null() {
        let mut json: *mut std::os::raw::c_char = std::ptr::null_mut();
        let mut json_len: usize = 0;
        let status = unsafe { fg_proto_to_json(proto_handle, &mut json, &mut json_len) };
        if status != 0 || json.is_null() || json_len == 0 {
            counters.abi_failures += 1;
        }
        unsafe { fg_free_string(json) };
    } else if status != 0 && !proto_handle.is_null() {
        counters.abi_failures += 1;
    }
    unsafe { fg_free_proto(proto_handle) };
}

fn main() -> ExitCode {
    let mut iterations: u64 = 200_000;
    let mut seed: u64 = 0x5EED_1234_9876_ABCD;
    let mut corpus_dirs: Vec<String> = Vec::new();
    let args: Vec<String> = std::env::args().collect();
    let mut index = 1;
    while let Some(arg) = args.get(index) {
        match arg.as_str() {
            "--iters" => {
                index += 1;
                iterations = args
                    .get(index)
                    .and_then(|text| text.parse::<u64>().ok())
                    .unwrap_or(iterations);
            }
            "--seed" => {
                index += 1;
                seed = args.get(index).and_then(|text| text.parse::<u64>().ok()).unwrap_or(seed);
            }
            "--corpus" => {
                index += 1;
                if let Some(dir) = args.get(index) {
                    corpus_dirs.push(dir.clone());
                }
            }
            other => {
                eprintln!("argumento desconhecido: {other}");
                return ExitCode::from(2);
            }
        }
        index += 1;
    }

    let seeds = load_corpus(&corpus_dirs);
    println!("corpus: {} arquivos de {}", seeds.len(), corpus_dirs.join(", "));

    let mut rng = Rng::new(seed);
    let mut counters = Counters::default();
    let started = Instant::now();

    for iteration in 0..iterations {
        let input = match iteration % 6 {
            0 => random_bytes(&mut rng, 256),
            1 => plist_like(&mut rng),
            2 => plist_trailer_fuzz(&mut rng),
            3 => {
                if seeds.is_empty() {
                    random_bytes(&mut rng, 64)
                } else {
                    let position = rng.below(seeds.len());
                    match seeds.get(position) {
                        Some(seed_bytes) => mutate(&mut rng, seed_bytes),
                        None => random_bytes(&mut rng, 64),
                    }
                }
            }
            4 => proto_like(&mut rng),
            _ => random_bytes(&mut rng, 24),
        };
        counters.inputs += 1;
        counters.bytes += u64::try_from(input.len()).unwrap_or(0);

        match std::panic::catch_unwind(AssertUnwindSafe(|| {
            formatguard::plist::parse(&input).and_then(|value| {
                formatguard::json::plist_to_json(&value).map(|_| ())
            })
        })) {
            Ok(Ok(())) => counters.plist_ok += 1,
            Ok(Err(_)) => counters.plist_err += 1,
            Err(_) => counters.panics += 1,
        }

        match std::panic::catch_unwind(AssertUnwindSafe(|| {
            formatguard::protobuf::parse(&input).and_then(|value| {
                formatguard::json::protobuf_to_json(&value).map(|_| ())
            })
        })) {
            Ok(Ok(())) => counters.proto_ok += 1,
            Ok(Err(_)) => counters.proto_err += 1,
            Err(_) => counters.panics += 1,
        }

        if iteration % 61 == 0 {
            match std::panic::catch_unwind(AssertUnwindSafe(|| abi_roundtrip(&input, &mut counters))) {
                Ok(()) => {}
                Err(_) => counters.panics += 1,
            }
        }
    }

    let elapsed = started.elapsed();
    let seconds = elapsed.as_secs_f64();
    println!("seed={seed} iters={iterations} corpus_seeds={}", seeds.len());
    println!("entradas={} bytes_gerados={}", counters.inputs, counters.bytes);
    println!("chamadas_de_parse={}", counters.inputs.saturating_mul(2));
    println!("plist_ok={} plist_err={}", counters.plist_ok, counters.plist_err);
    println!("proto_ok={} proto_err={}", counters.proto_ok, counters.proto_err);
    println!("abi_roundtrips={} abi_falhas={}", counters.abi_calls, counters.abi_failures);
    println!("panics={}", counters.panics);
    println!(
        "tempo={:.2}s entradas_por_segundo={:.0}",
        seconds,
        if seconds > 0.0 { iterations as f64 / seconds } else { 0.0 }
    );
    if counters.panics > 0 || counters.abi_failures > 0 {
        return ExitCode::from(1);
    }
    ExitCode::SUCCESS
}
