//! `fgdump` — dump canonico (JSON) de um arquivo, ou so o codigo de erro.
//!
//! Uso:
//!   fgdump plist <arquivo>
//!   fgdump proto <arquivo>
//!
//! Codigo de saida: 0 = parse ok, 1 = parse recusado, 2 = uso/IO.
//! A linha de diagnostico vai para stderr ("status=<n> <mensagem>").

use std::process::ExitCode;

fn usage() {
    eprintln!("uso: fgdump plist|proto <arquivo>");
}

fn run(kind: &str, path: &str) -> ExitCode {
    let data = match std::fs::read(path) {
        Ok(data) => data,
        Err(error) => {
            eprintln!("nao foi possivel ler {path}: {error}");
            return ExitCode::from(2);
        }
    };
    match kind {
        "plist" => match formatguard::plist::parse(&data) {
            Ok(value) => match formatguard::json::plist_to_json(&value) {
                Ok(json) => {
                    match std::io::Write::write_all(&mut std::io::stdout(), &json) {
                        Ok(()) => ExitCode::SUCCESS,
                        Err(error) => {
                            eprintln!("falha de escrita: {error}");
                            ExitCode::from(2)
                        }
                    }
                }
                Err(error) => {
                    eprintln!("status={} {}", error.status().as_i32(), error.message());
                    ExitCode::from(1)
                }
            },
            Err(error) => {
                eprintln!("status={} {}", error.status().as_i32(), error.message());
                ExitCode::from(1)
            }
        },
        "proto" => match formatguard::protobuf::parse(&data) {
            Ok(value) => match formatguard::json::protobuf_to_json(&value) {
                Ok(json) => match std::io::Write::write_all(&mut std::io::stdout(), &json) {
                    Ok(()) => ExitCode::SUCCESS,
                    Err(error) => {
                        eprintln!("falha de escrita: {error}");
                        ExitCode::from(2)
                    }
                },
                Err(error) => {
                    eprintln!("status={} {}", error.status().as_i32(), error.message());
                    ExitCode::from(1)
                }
            },
            Err(error) => {
                eprintln!("status={} {}", error.status().as_i32(), error.message());
                ExitCode::from(1)
            }
        },
        _ => {
            usage();
            ExitCode::from(2)
        }
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    match (args.get(1).map(String::as_str), args.get(2).map(String::as_str)) {
        (Some(kind), Some(path)) => run(kind, path),
        _ => {
            usage();
            ExitCode::from(2)
        }
    }
}
