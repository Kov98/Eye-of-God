#!/usr/bin/env python3
"""Gera o corpus do formatguard com oraculos independentes do Rust.

Tres saidas, todas dentro de rust/formatguard/tests/corpus/:

  gen/NN-nome.plist + NN-nome.json
      plists escritos por plistlib (CPython 3.13, FMT_BINARY) e o JSON canonico
      esperado calculado a partir do que o plistlib LE de volta. Ou seja: escritor e
      leitor de plist de terceiro, nao o codigo Rust sob teste.

  proto/NN-nome.bin + NN-nome.json
      vetores protobuf montados por um codificador escrito aqui (varint, zigzag, fixed32,
      fixed64, length-delimited e grupos) e o JSON canonico esperado.

  mut/mNNNNN.bin
      mutacoes estruturadas dos plists legitimos (corpus hostil para o teste diferencial).

Uso:
    python tools/gen_corpus.py [--mutations N] [--seed S]
"""
import argparse
import datetime
import os
import plistlib
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CRATE = os.path.dirname(HERE)
CORPUS = os.path.join(CRATE, "tests", "corpus")
EPOCH = datetime.datetime(2001, 1, 1)

# ---------------------------------------------------------------- JSON canonico
# Regra identica a src/json.rs (e ao dumper do harness C++ em .oracle_rust).

ESCAPES = {
    '"': '\\"',
    "\\": "\\\\",
    "\b": "\\b",
    "\f": "\\f",
    "\n": "\\n",
    "\r": "\\r",
    "\t": "\\t",
}


def exact_real(value):
    """Expansao decimal exata do double, sem zeros a direita."""
    if value != value:
        return "nan"
    if value == float("inf"):
        return "inf"
    if value == float("-inf"):
        return "-inf"
    bits = struct.unpack("<Q", struct.pack("<d", value))[0]
    exp_field = (bits >> 52) & 0x7FF
    frac = bits & ((1 << 52) - 1)
    if exp_field == 0 and frac == 0:
        return "-0" if (bits >> 63) else "0"
    exp2 = -1074 if exp_field == 0 else exp_field - 1075
    text = format(value, ".0f") if exp2 >= 0 else format(value, "." + str(-exp2) + "f")
    if "." in text:
        text = text.rstrip("0")
        if text.endswith("."):
            text = text[:-1]
    return text


def escape_text(text):
    out = []
    for ch in text:
        if ch in ESCAPES:
            out.append(ESCAPES[ch])
        elif ord(ch) < 0x20:
            out.append("\\u%04x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def canonical(value):
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            return '"%s"' % exact_real(value)
        return exact_real(value)
    if isinstance(value, plistlib.UID):
        return '{"$uid":%d}' % value.data
    if isinstance(value, datetime.datetime):
        seconds = (value - EPOCH).total_seconds()
        return '{"$date":%s}' % exact_real(seconds)
    if isinstance(value, bytes):
        return '{"$data":"%s"}' % value.hex()
    if isinstance(value, str):
        return '"' + escape_text(value) + '"'
    if isinstance(value, list):
        return "[" + ",".join(canonical(item) for item in value) + "]"
    if isinstance(value, dict):
        parts = []
        for key, item in value.items():
            name = key if isinstance(key, str) else str(key)
            body = escape_text(name[1:]) if name.startswith("$") else escape_text(name)
            prefix = '"\\u0024' if name.startswith("$") else '"'
            parts.append(prefix + body + '":' + canonical(item))
        return "{" + ",".join(parts) + "}"
    raise TypeError("tipo nao canonico: %r" % (type(value),))


# --------------------------------------------------------------------- corpus
def _nest(levels):
    value = {"leaf": 1}
    for _ in range(levels - 1):
        value = {"n": value}
    return value


def plist_cases():
    long_text = "a" * 70000
    return [
        (
            "types",
            {
                "Null": None,
                "False": False,
                "True": True,
                "Zero": 0,
                "One": 127,
                "ByteMax": 255,
                "Short": 65535,
                "Int32": 2147483647,
                "Int64": 9223372036854775807,
                "Negative": -1,
                "NegativeBig": -9223372036854775808,
                "RealZero": 0.0,
                "RealHalf": 1.5,
                "RealTenth": 0.1,
                "RealHuge": 1e300,
                "RealTiny": 1e-300,
                "RealPi": 3.141592653589793,
                "Date": datetime.datetime(2020, 9, 13, 12, 26, 40),
                "DataEmpty": b"",
                "DataByte": b"\x00",
                "Data15": bytes(range(15)),
                "Data16": bytes(range(16)),
                "Data300": bytes(range(256)) + bytes(44),
                "TextEmpty": "",
                "TextAscii": "msgstore",
                "Text15": "b" * 15,
                "TextAccents": "acao, coracao, ao",
                "TextCJK": "kanji tesuto",
                "TextEmoji": "lock",
                "ArrayEmpty": [],
                "ArrayMixed": [1, "two", 3.5, True, None, b"\xde\xad"],
                "DictEmpty": {},
                "Uid": plistlib.UID(42),
                "DollarKey": {"$notareserved": 1},
            },
        ),
        ("nested3", {"a": {"b": {"c": [1, {"d": True}]}}}),
        ("deep40", _nest(40)),
        ("wide", {("k%03d" % index): index for index in range(300)}),
        (
            "bigdata",
            {"Blob": bytes((index % 251) for index in range(70000)), "Text": long_text},
        ),
        ("array20", [index * 1.5 for index in range(20)]),
        (
            "keyed_archive",
            {
                "$version": 100000,
                "$archiver": "NSKeyedArchiver",
                "$top": {"root": plistlib.UID(1)},
                "$objects": [
                    "$null",
                    {"NS.data": b"\x01\x02\x03", "NS.string": "texto"},
                    {"$classname": "NSMutableData"},
                ],
            },
        ),
        (
            "strings_utf16",
            {"a": "\u00e7\u00e3o", "b": "\u65e5\u672c\u8a9e", "c": "mixed ascii + \u00e7\u00e3o", "d": "\U0001F4A9 lock"},
        ),
        ("empty_container", {"List": [], "Dict": {}, "Text": "", "Data": b""}),
    ]


def generate_plists():
    out_dir = os.path.join(CORPUS, "gen")
    os.makedirs(out_dir, exist_ok=True)
    for name in os.listdir(out_dir):
        if name.endswith((".plist", ".json")):
            os.remove(os.path.join(out_dir, name))
    written = 0
    mismatches = 0
    for index, (name, value) in enumerate(plist_cases()):
        path = os.path.join(out_dir, "%02d-%s.plist" % (index, name))
        with open(path, "wb") as handle:
            plistlib.dump(value, handle, fmt=plistlib.FMT_BINARY, sort_keys=False)
        with open(path, "rb") as handle:
            raw = handle.read()
        if not raw.startswith(b"bplist00"):
            raise SystemExit("plistlib nao escreveu bplist00 em %s" % path)
        # leitor independente: o esperado vem do que o plistlib le de volta
        with open(path, "rb") as handle:
            readback = plistlib.load(handle)
        expected = canonical(readback)
        original = canonical(value)
        if expected != original:
            mismatches += 1
            print("  AVISO: ida-e-volta do plistlib divergiu em %s" % name)
        with open(os.path.join(out_dir, "%02d-%s.json" % (index, name)), "w", encoding="utf-8") as handle:
            handle.write(expected)
        written += 1
    return written, mismatches


# ------------------------------------------------------------------ protobuf
def pb_varint(value):
    value &= 0xFFFFFFFFFFFFFFFF
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def pb_zigzag(value):
    """Codifica um sinal em zigzag (usado pelo sintN)."""
    return ((value << 1) ^ (value >> 63)) & 0xFFFFFFFFFFFFFFFF


def pb_zigzag_decode(raw):
    """Decodifica um varint em zigzag, exatamente como o parser Rust faz."""
    value = raw & 0xFFFFFFFFFFFFFFFF
    decoded = (value >> 1) ^ (-(value & 1) & 0xFFFFFFFFFFFFFFFF)
    return decoded - (1 << 64) if decoded >= (1 << 63) else decoded


def pb_key(number, wire):
    return pb_varint((number << 3) | wire)


class Message:
    """Codificador protobuf independente, que ja monta o JSON esperado."""

    def __init__(self):
        self.raw = bytearray()
        self.fields = []

    def varint(self, number, value):
        self.raw += pb_key(number, 0) + pb_varint(value)
        unsigned = value & 0xFFFFFFFFFFFFFFFF
        self.fields.append(
            '{"n":%d,"w":0,"i":%d,"z":%d}' % (number, unsigned, pb_zigzag_decode(unsigned))
        )
        return self

    def sint(self, number, value):
        return self.varint(number, pb_zigzag(value))

    def fixed64(self, number, value):
        self.raw += pb_key(number, 1) + struct.pack("<Q", value)
        self.fields.append('{"n":%d,"w":1,"i":%d}' % (number, value))
        return self

    def fixed32(self, number, value):
        self.raw += pb_key(number, 5) + struct.pack("<I", value)
        self.fields.append('{"n":%d,"w":5,"i":%d}' % (number, value))
        return self

    def payload(self, number, data):
        self.raw += pb_key(number, 2) + pb_varint(len(data)) + data
        self.fields.append('{"n":%d,"w":2,"len":%d,"hex":"%s"}' % (number, len(data), data.hex()))
        return self

    def group(self, number, inner):
        self.raw += pb_key(number, 3) + inner.raw + pb_key(number, 4)
        self.fields.append('{"n":%d,"w":3,"fields":%s}' % (number, inner.fields_json()))
        return self

    def fields_json(self):
        return "[" + ",".join(self.fields) + "]"

    def json(self):
        return '{"fields":' + self.fields_json() + "}"

    def bytes(self):
        return bytes(self.raw)


def proto_cases():
    cases = []
    cases.append(("empty", Message()))
    cases.append(("one_varint", Message().varint(1, 150)))
    cases.append(
        (
            "all_wire_types",
            Message()
            .varint(1, 300)
            .fixed64(2, 0x0102030405060708)
            .payload(3, b"whatsapp msgstore")
            .fixed32(4, 0xDEADBEEF)
            .varint(536870911, 1),
        )
    )
    cases.append(
        (
            "zigzag_values",
            Message().sint(1, 0).sint(2, -1).sint(3, 1).sint(4, -2147483648).sint(5, 2147483647),
        )
    )
    cases.append(("varint_extremes", Message().varint(1, 0).varint(2, 1).varint(3, 0xFFFFFFFFFFFFFFFF)))
    nested = Message().varint(1, 7)
    cases.append(("group_simple", Message().group(5, nested)))
    inner = Message().payload(2, b"\x00\xff").varint(3, 9)
    middle = Message().group(4, inner).varint(1, 1)
    cases.append(("group_nested", Message().group(6, middle).fixed32(7, 42)))
    cases.append(
        (
            "repeated",
            Message().varint(1, 1).varint(1, 2).varint(1, 3).payload(1, b"abc").payload(1, b"abc"),
        )
    )
    big = bytes(((index * 7) % 256) for index in range(3000))
    cases.append(("big_payload", Message().payload(1, big).payload(2, b"")))
    cases.append(
        (
            "chat_like",
            Message()
            .varint(1, 0x5F5E100)
            .payload(2, b"5511999999999@s.whatsapp.net")
            .varint(3, 1600000000)
            .payload(4, b"\x0a\x05hello"),
        )
    )
    return cases


def generate_protos():
    out_dir = os.path.join(CORPUS, "proto")
    os.makedirs(out_dir, exist_ok=True)
    for name in os.listdir(out_dir):
        if name.endswith((".bin", ".json")):
            os.remove(os.path.join(out_dir, name))
    written = 0
    for index, (name, message) in enumerate(proto_cases()):
        with open(os.path.join(out_dir, "%02d-%s.bin" % (index, name)), "wb") as handle:
            handle.write(message.bytes())
        with open(os.path.join(out_dir, "%02d-%s.json" % (index, name)), "w", encoding="utf-8") as handle:
            handle.write(message.json())
        written += 1
    return written


# ------------------------------------------------------------------ mutacoes
def read_seeds():
    seeds = []
    oracle = os.path.join(os.path.dirname(os.path.dirname(CRATE)), ".oracle_ios", "fixture")
    for sub in ("plain", "enc"):
        directory = os.path.join(oracle, sub)
        if not os.path.isdir(directory):
            continue
        for name in os.listdir(directory):
            path = os.path.join(directory, name)
            if not os.path.isfile(path):
                continue
            with open(path, "rb") as handle:
                data = handle.read(1 << 20)
            if data.startswith(b"bplist00"):
                seeds.append(("oracle_%s_%s" % (sub, name), data))
    gen = os.path.join(CORPUS, "gen")
    if os.path.isdir(gen):
        for name in sorted(os.listdir(gen)):
            if not name.endswith(".plist"):
                continue
            path = os.path.join(gen, name)
            with open(path, "rb") as handle:
                seeds.append(("gen_" + name, handle.read(1 << 20)))
    return seeds


def mutate(data, rng):
    out = bytearray(data)
    rounds = rng.randint(1, 6)
    for _ in range(rounds):
        kind = rng.randrange(8)
        if kind == 0 and out:
            position = rng.randrange(len(out))
            out[position] ^= 1 << rng.randrange(8)
        elif kind == 1 and out:
            out[rng.randrange(len(out))] = rng.randrange(256)
        elif kind == 2 and out:
            position = rng.randrange(len(out))
            end = min(len(out), position + rng.randint(1, 32))
            del out[position:end]
        elif kind == 3:
            position = rng.randrange(len(out) + 1)
            out[position:position] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 8)))
        elif kind == 4:
            out = out[: rng.randrange(len(out) + 1)]
        elif kind == 5 and len(out) > 40:
            trailer = len(out) - 32
            slot = trailer + rng.choice([6, 7, 8, 16, 24, 26, 28, 30])
            out[slot] = rng.randrange(256)
        elif kind == 6 and len(out) > 40:
            table = len(out) - 32 - rng.randint(1, 8)
            if 8 <= table < len(out) - 32:
                out[table] = rng.randrange(256)
        elif kind == 7 and out:
            position = rng.randrange(len(out))
            end = min(len(out), position + rng.randint(1, 64))
            for slot in range(position, end):
                out[slot] = 0
    return bytes(out)


def generate_mutations(count, seed):
    seeds = read_seeds()
    if not seeds:
        raise SystemExit("nenhum plist legitimo encontrado para mutar")
    out_dir = os.path.join(CORPUS, "mut")
    os.makedirs(out_dir, exist_ok=True)
    for name in os.listdir(out_dir):
        if name.endswith(".bin"):
            os.remove(os.path.join(out_dir, name))
    rng = random.Random(seed)
    for index in range(count):
        _, data = seeds[rng.randrange(len(seeds))]
        mutated = mutate(data, rng)
        with open(os.path.join(out_dir, "m%05d.bin" % index), "wb") as handle:
            handle.write(mutated)
    return count, len(seeds)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutations", type=int, default=4000)
    parser.add_argument("--seed", type=int, default=20260913)
    args = parser.parse_args()

    plists, mismatch = generate_plists()
    print("plists gerados: %d (divergencias de ida-e-volta do plistlib: %d)" % (plists, mismatch))
    protos = generate_protos()
    print("vetores protobuf gerados: %d" % protos)
    mutations, seeds = generate_mutations(args.mutations, args.seed)
    print("mutacoes geradas: %d a partir de %d plists legitimos" % (mutations, seeds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
