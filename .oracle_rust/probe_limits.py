#!/usr/bin/env python3
"""Gera as duas entradas hostis que atacam defeitos encontrados na leitura de ios_backup.cpp.

1. utf16_overflow.plist
   Objeto 0x6F (string UTF-16 com comprimento estendido) cujo comprimento e 2^63. O C++
   faz "units * 2 > data_.size() - pos" com uint64: 2^63 * 2 transborda para 0, a checagem
   passa e ele chama utf16be_to_utf8() com units = 2^63. O Rust faz a conta com
   checked_mul e recusa com BadLength.

2. dag_depthN.plist
   Arrays de 2 elementos apontando um para o outro (referencias compartilhadas, sem
   ciclo). Sao 2^N nos visitados a partir de um arquivo de ~(3*N)+40 bytes. O C++ nao tem
   orcamento de nos: aceita e expande tudo. O Rust recusa acima de 1 048 576 nos. A
   profundidade maxima 64 do formato permite 2^64 nos.

Uso: python .oracle_rust/probe_limits.py [--depths 14,16,18,20,22]
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "results", "probes")


def trailer(objects, top, offset_size, ref_size):
    blob = bytearray(b"bplist00")
    offsets = []
    for obj in objects:
        offsets.append(len(blob))
        blob += obj
    offset_table = len(blob)
    for offset in offsets:
        blob += offset.to_bytes(offset_size, "big")
    tail = bytearray(32)
    tail[6] = offset_size
    tail[7] = ref_size
    tail[8:16] = len(objects).to_bytes(8, "big")
    tail[16:24] = top.to_bytes(8, "big")
    tail[24:32] = offset_table.to_bytes(8, "big")
    return bytes(blob + tail)


def utf16_overflow():
    # objeto 0: 0x6F (string UTF-16, comprimento estendido) + inteiro de 8 bytes 2^63
    units = 1 << 63
    obj = bytearray([0x6F, 0x13])
    obj += units.to_bytes(8, "big")
    obj += b"\x00\x41\x00\x42"  # 4 bytes de payload, para a checagem do C++ passar
    return trailer([bytes(obj)], 0, 1, 1)


def dag(depth):
    """2^depth nos: cada nivel aponta duas vezes para o proximo."""
    objects = []
    ref_size = 1
    for level in range(depth):
        objects.append(bytes([0xA2, level + 1, level + 1]))
    objects.append(bytes([0x00]))  # folha nula
    if len(objects) > 255:
        raise SystemExit("profundidade grande demais para ref_size 1")
    return trailer(objects, 0, 2, ref_size)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--depths", default="14,16,18,20,22")
    args = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)

    path = os.path.join(OUT, "utf16_overflow.plist")
    with open(path, "wb") as handle:
        handle.write(utf16_overflow())
    print("utf16_overflow.plist: %d bytes" % os.path.getsize(path))

    for text in args.depths.split(","):
        depth = int(text)
        path = os.path.join(OUT, "dag_depth%02d.plist" % depth)
        data = dag(depth)
        with open(path, "wb") as handle:
            handle.write(data)
        print(
            "dag_depth%02d.plist: %d bytes, 2^%d = %d nos declarados"
            % (depth, len(data), depth + 1, (1 << (depth + 1)) - 1)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
