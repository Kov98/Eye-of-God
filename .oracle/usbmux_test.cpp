// language: C++, file: .oracle/usbmux_test.cpp
// Prova do protocolo sem aparelho: quadros montados e parseados byte a byte,
// mais um DeviceList no formato que o usbmuxd responde.
#include "usbmux.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace olho::usbmux;

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    // 1. ida e volta de um frame Connect
    {
        frame f;
        f.type = message::connect;
        f.tag = 7;
        f.payload = connect_request(3, 62078);
        bytes wire;
        std::string err;
        check("encode connect", encode(f, wire, err));
        check("tamanho do campo length bate", wire.size() == 4 + 16 + f.payload.size());
        check("length do cabecalho = total", (wire[0] | (wire[1] << 8)) == (int)wire.size());

        frame back;
        std::size_t used = 0;
        check("decode connect", decode(wire.data(), wire.size(), back, used, err));
        check("payload preservado", back.payload == f.payload);
        check("tag preservado", back.tag == 7);
        check("tipo preservado", back.type == message::connect);
    }

    // 2. dois frames no mesmo buffer: o caller tem que conseguir andar
    {
        frame a; a.type = message::listen; a.tag = 1; a.payload = list_devices_request();
        frame b; b.type = message::connect; b.tag = 2; b.payload = connect_request(9, 1234);
        bytes wire;
        std::string err;
        encode(a, wire, err);
        bytes tmp;
        encode(b, tmp, err);
        wire.insert(wire.end(), tmp.begin(), tmp.end());

        std::size_t off = 0, used = 0;
        frame one, two;
        check("primeiro frame", decode(wire.data() + off, wire.size() - off, one, used, err));
        off += used;
        check("segundo frame", decode(wire.data() + off, wire.size() - off, two, used, err));
        check("tags na ordem", one.tag == 1 && two.tag == 2);
    }

    // 3. DeviceList real no formato do usbmuxd, com ordem de chaves variada
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict><key>DeviceList</key><array>\n"
        "<dict><key>DeviceID</key><integer>3</integer>"
        "<key>MessageType</key><string>Attached</string>"
        "<key>Properties</key><dict><key>LocationID</key><integer>0x14100000</integer></dict>"
        "<key>SerialNumber</key><string>F17ABCDEFGHI</string>"
        "<key>ConnectionType</key><string>USB</string>"
        "<key>ProductID</key><integer>4776</integer></dict>\n"
        "<dict><key>ConnectionType</key><string>Network</string>"
        "<key>SerialNumber</key><string>00008101-001A2B3C4D5E6F70</string>"
        "<key>ProductID</key><integer>4778</integer>"
        "<key>DeviceID</key><integer>11</integer></dict>\n"
        "</array></dict></plist>";

    {
        std::vector<device_entry> devices;
        std::string err;
        check("parse DeviceList", parse_device_list(reply, devices, err));
        check("dois aparelhos", devices.size() == 2);
        if (devices.size() == 2) {
            check("serial do primeiro", devices[0].serial == "F17ABCDEFGHI");
            check("device id do primeiro", devices[0].device_id == 3);
            check("conexao do primeiro", devices[0].connection == "USB");
            check("product id do primeiro", devices[0].product_id == 4776);
            check("ordem de chave nao importa", devices[1].serial == "00008101-001A2B3C4D5E6F70" &&
                                                 devices[1].device_id == 11 &&
                                                 devices[1].connection == "Network");
        }
    }

    // 4. Result: sucesso e recusa
    {
        std::uint32_t result = 0; std::uint16_t port = 0; std::string err;
        check("Result sucesso", parse_result(
            "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
            "<key>MessageType</key><string>Result</string>"
            "<key>Number</key><integer>0</integer>"
            "<key>Result</key><integer>0</integer>"
            "<key>PortNumber</key><integer>50000</integer></dict></plist>", result, port, err));
        check("result = 0 e porta 50000", result == 0 && port == 50000);

        check("Result recusa", parse_result(
            "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
            "<key>Result</key><integer>3</integer></dict></plist>", result, port, err));
        check("result = 3 lido", result == 3);
    }

    // 5. quadros hostis: tem que recusar, nunca confiar
    {
        frame out; std::size_t used = 0; std::string err;
        std::uint8_t shortbuf[10] = {0};
        check("cabecalho truncado recusado", !decode(shortbuf, sizeof(shortbuf), out, used, err));

        bytes liar(24, 0);
        liar[0] = 200;                     // length diz 200, buffer tem 24
        check("length mentindo recusado", !decode(liar.data(), liar.size(), out, used, err));

        bytes over(24, 0);
        over[0] = 24;                      // frame ok...
        over[16] = 200;                    // ...mas plist diz 200 bytes
        check("plist_length maior que o frame recusado",
              !decode(over.data(), over.size(), out, used, err));

        bytes zero(20, 0);                 // length 0
        check("length zero recusado", !decode(zero.data(), zero.size(), out, used, err));
    }

    // 6. plist hostil
    {
        std::vector<device_entry> devices;
        std::string err;
        check("plist sem DeviceList recusado",
              !parse_device_list("<?xml version=\"1.0\"?><plist><dict><key>X</key><string>y</string></dict></plist>",
                                 devices, err));
        check("lixo recusado", !parse_device_list("nao e plist", devices, err));
    }

    std::printf(failures == 0 ? "USBMUX OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}