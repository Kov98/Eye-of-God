//! Testes do parser protobuf: varint, zigzag, fixed, length-delimited e grupos.

use formatguard::error::FgStatus;
use formatguard::protobuf::{parse, zigzag_decode, ProtoValue, WireType};

fn varint(mut value: u64) -> Vec<u8> {
    let mut out = Vec::new();
    loop {
        let mut byte = (value & 0x7F) as u8;
        value >>= 7;
        if value != 0 {
            byte |= 0x80;
        }
        out.push(byte);
        if value == 0 {
            return out;
        }
    }
}

fn key(number: u32, wire: u8) -> Vec<u8> {
    varint((u64::from(number) << 3) | u64::from(wire))
}

fn length_delimited(number: u32, payload: &[u8]) -> Vec<u8> {
    let mut out = key(number, 2);
    out.extend_from_slice(&varint(payload.len() as u64));
    out.extend_from_slice(payload);
    out
}

fn status_of(blob: &[u8]) -> FgStatus {
    match parse(blob) {
        Ok(_) => FgStatus::Ok,
        Err(error) => error.status(),
    }
}

#[test]
fn empty_buffer_is_an_empty_message() {
    let message = parse(&[]).expect("buffer vazio e mensagem vazia");
    assert!(message.is_empty());
    assert_eq!(
        String::from_utf8_lossy(&formatguard::json::protobuf_to_json(&message).unwrap()),
        r#"{"fields":[]}"#
    );
}

#[test]
fn varint_basics() {
    let mut blob = key(1, 0);
    blob.extend_from_slice(&varint(150));
    let message = parse(&blob).expect("varint simples");
    assert_eq!(message.len(), 1);
    let field = message.first(1).expect("campo 1");
    assert_eq!(field.wire, WireType::Varint);
    assert_eq!(field.value, ProtoValue::Varint(150));

    // u64::MAX em 10 bytes
    let mut blob = key(2, 0);
    blob.extend_from_slice(&varint(u64::MAX));
    assert_eq!(blob.len(), 1 + 10);
    let message = parse(&blob).expect("varint de 10 bytes");
    assert_eq!(message.first(2).map(|field| field.value.clone()), Some(ProtoValue::Varint(u64::MAX)));

    // 10 bytes em que o decimo NAO tem bit de continuacao mas tem bits altos: o valor
    // e aceito e os bits acima do 63 sao descartados (igual ao protobuf de referencia).
    let mut blob = key(3, 0);
    blob.extend_from_slice(&[0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F]);
    let message = parse(&blob).expect("varint de 10 bytes");
    assert_eq!(
        message.first(3).map(|field| field.value.clone()),
        Some(ProtoValue::Varint(1 << 63))
    );

    // 10 bytes em que o decimo ainda tem bit de continuacao: varint malformado.
    let mut blob = key(3, 0);
    blob.extend_from_slice(&[0xFF; 10]);
    assert_eq!(status_of(&blob), FgStatus::BadVarint);

    // 11 bytes: recusado
    let mut blob = key(4, 0);
    blob.extend_from_slice(&[0x80; 11]);
    assert_eq!(status_of(&blob), FgStatus::BadVarint);

    // varint truncado: continuacao no ultimo byte
    let mut blob = key(5, 0);
    blob.push(0x80);
    assert_eq!(status_of(&blob), FgStatus::BadVarint);

    // chave truncada
    assert_eq!(status_of(&[0x08]), FgStatus::BadVarint);
}

#[test]
fn fixed32_and_fixed64_are_little_endian() {
    let mut blob = key(1, 1);
    blob.extend_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
    blob.extend_from_slice(&key(2, 5));
    blob.extend_from_slice(&[0xAA, 0xBB, 0xCC, 0xDD]);
    let message = parse(&blob).expect("fixed");
    assert_eq!(
        message.first(1).map(|field| field.value.clone()),
        Some(ProtoValue::Fixed64(0x0807_0605_0403_0201))
    );
    assert_eq!(
        message.first(2).map(|field| field.value.clone()),
        Some(ProtoValue::Fixed32(0xDDCC_BBAA))
    );

    // fixed64 truncado
    let mut blob = key(1, 1);
    blob.extend_from_slice(&[1, 2, 3]);
    assert_eq!(status_of(&blob), FgStatus::Truncated);
    let mut blob = key(1, 5);
    blob.extend_from_slice(&[1, 2]);
    assert_eq!(status_of(&blob), FgStatus::Truncated);
}

#[test]
fn length_delimited_and_zigzag() {
    let blob = length_delimited(3, b"whatsapp");
    let message = parse(&blob).expect("length-delimited");
    assert_eq!(
        message.first(3).map(|field| field.value.clone()),
        Some(ProtoValue::Bytes(b"whatsapp".to_vec()))
    );

    // comprimento maior que o buffer
    let mut blob = key(3, 2);
    blob.push(10);
    blob.extend_from_slice(b"abc");
    assert_eq!(status_of(&blob), FgStatus::BadLength);

    // comprimento absurdo (nao cabe em usize / passa do buffer)
    let mut blob = key(3, 2);
    blob.extend_from_slice(&varint(u64::MAX));
    assert_eq!(status_of(&blob), FgStatus::BadLength);

    // zigzag
    let expected = [(0_u64, 0_i64), (1, -1), (2, 1), (3, -2), (4, 2), (u64::MAX, i64::MIN)];
    for (raw, value) in expected {
        assert_eq!(zigzag_decode(raw), value, "zigzag {raw}");
        assert_eq!(ProtoValue::Varint(raw).zigzag(), Some(value));
    }
    assert_eq!(ProtoValue::Fixed32(0).zigzag(), None);

    // sint64 -1 (zigzag 1) na mensagem
    let mut blob = key(7, 0);
    blob.extend_from_slice(&varint(1));
    let message = parse(&blob).expect("sint64");
    let field = message.first(7).expect("campo 7");
    assert_eq!(field.value.zigzag(), Some(-1));
}

#[test]
fn field_numbers_and_wire_types_are_validated() {
    // campo 0
    assert_eq!(status_of(&[0x00, 0x01]), FgStatus::BadFieldNumber);
    // campo acima de 2^29-1
    let mut blob = varint((1_u64 << 29) << 3);
    blob.push(0x01);
    assert_eq!(status_of(&blob), FgStatus::BadFieldNumber);
    // wire types 6 e 7
    assert_eq!(status_of(&[0x0E]), FgStatus::BadWireType);
    assert_eq!(status_of(&[0x0F]), FgStatus::BadWireType);
    // maior campo valido aceito
    let mut blob = varint(((1_u64 << 29) - 1) << 3);
    blob.push(0x01);
    assert_eq!(status_of(&blob), FgStatus::Ok);
}

#[test]
fn groups_nest_and_close() {
    let mut blob = key(4, 3);
    blob.extend_from_slice(&key(1, 0));
    blob.extend_from_slice(&varint(9));
    blob.extend_from_slice(&key(4, 4));
    let message = parse(&blob).expect("grupo simples");
    let field = message.first(4).expect("campo 4");
    assert_eq!(field.wire, WireType::StartGroup);
    match &field.value {
        ProtoValue::Group(inner) => {
            assert_eq!(inner.first(1).map(|item| item.value.clone()), Some(ProtoValue::Varint(9)));
        }
        other => panic!("esperado grupo, veio {other:?}"),
    }
    assert_eq!(message.len(), 1, "END_GROUP nao vira campo");

    // grupo aninhado 2 niveis e campo depois do grupo
    let mut blob = key(1, 3);
    blob.extend_from_slice(&key(2, 3));
    blob.extend_from_slice(&key(3, 0));
    blob.extend_from_slice(&varint(5));
    blob.extend_from_slice(&key(2, 4));
    blob.extend_from_slice(&key(1, 4));
    blob.extend_from_slice(&key(9, 0));
    blob.extend_from_slice(&varint(7));
    let message = parse(&blob).expect("grupos aninhados");
    assert_eq!(message.len(), 2);
    assert_eq!(message.first(9).map(|field| field.value.clone()), Some(ProtoValue::Varint(7)));
}

#[test]
fn malformed_groups_are_rejected() {
    // grupo sem END_GROUP
    let mut blob = key(1, 3);
    blob.extend_from_slice(&key(2, 0));
    blob.extend_from_slice(&varint(1));
    assert_eq!(status_of(&blob), FgStatus::BadGroup);

    // END_GROUP com numero diferente
    let mut blob = key(1, 3);
    blob.extend_from_slice(&key(2, 4));
    assert_eq!(status_of(&blob), FgStatus::BadGroup);

    // END_GROUP sem grupo aberto
    let mut blob = key(1, 4);
    blob.extend_from_slice(&key(9, 0));
    assert_eq!(status_of(&blob), FgStatus::BadGroup);
}

#[test]
fn group_depth_is_limited() {
    fn nested(levels: usize) -> Vec<u8> {
        let mut blob = Vec::new();
        for _ in 0..levels {
            blob.extend_from_slice(&key(1, 3));
        }
        for _ in 0..levels {
            blob.extend_from_slice(&key(1, 4));
        }
        blob
    }
    assert_eq!(status_of(&nested(64)), FgStatus::Ok);
    assert_eq!(status_of(&nested(65)), FgStatus::DepthLimit);
}

#[test]
fn json_shape_is_documented_shape() {
    let mut blob = key(1, 0);
    blob.extend_from_slice(&varint(2)); // zigzag 1
    blob.extend_from_slice(&key(2, 1));
    blob.extend_from_slice(&[0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    blob.extend_from_slice(&length_delimited(3, &[0xDE, 0xAD]));
    let message = parse(&blob).expect("mensagem mista");
    let json = formatguard::json::protobuf_to_json(&message).expect("json");
    assert_eq!(
        String::from_utf8_lossy(&json),
        concat!(
            r#"{"fields":[{"n":1,"w":0,"i":2,"z":1},"#,
            r#"{"n":2,"w":1,"i":1},"#,
            r#"{"n":3,"w":2,"len":2,"hex":"dead"}]}"#
        )
    );
}

#[test]
fn random_inputs_never_panic() {
    let mut state: u64 = 0x1234_5678_9ABC_DEF0;
    for _ in 0..2000 {
        let mut blob = Vec::new();
        for _ in 0..16 {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            blob.push((state & 0xFF) as u8);
        }
        let _ = parse(&blob);
    }
}
