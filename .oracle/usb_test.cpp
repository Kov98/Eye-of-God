// language: C++, file: .oracle/usb_test.cpp
// Enumera de verdade o barramento USB desta maquina. Sem Apple device o esperado
// e: ok, zero dispositivos, e nenhum handle vazado.
#include "ios_usb.hpp"

#include <windows.h>
#include <psapi.h>

#include <cstdio>

int main() {
    HANDLE before = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS pmc0{}, pmc1{};
    GetProcessMemoryInfo(before, &pmc0, sizeof(pmc0));

    const auto first = olho::ios::enumerate_apple_devices();
    std::printf("enumeracao: ok=%d  dispositivos=%zu  notas=%zu\n",
                (int)first.ok, first.devices.size(), first.notes.size());
    if (!first.error.empty()) std::printf("  erro: %s\n", first.error.c_str());
    for (const auto& n : first.notes) std::printf("  nota: %s\n", n.c_str());
    for (const auto& d : first.devices) {
        std::printf("  device: vid=%04x pid=%04x serial=%s iface=%u in=0x%02x out=0x%02x\n",
                    d.vid, d.pid, d.serial.c_str(), d.interface_number,
                    d.endpoint_in, d.endpoint_out);
    }

    // 200 ciclos: se a enumeracao vazasse handle, o processo morreria ou incharia.
    for (int i = 0; i < 200; ++i) {
        const auto r = olho::ios::enumerate_apple_devices();
        if (!r.ok) { std::printf("FALHA no ciclo %d: %s\n", i, r.error.c_str()); return 1; }
    }
    GetProcessMemoryInfo(before, &pmc1, sizeof(pmc1));
    const long long delta = (long long)pmc1.WorkingSetSize - (long long)pmc0.WorkingSetSize;
    std::printf("200 ciclos de enumeracao: memoria variou %+lld KB\n", delta / 1024);

    // Abrir um caminho inexistente tem que falhar limpo, nao travar.
    olho::ios::usb_device fake;
    fake.path = "\\\\?\\usb#vid_05ac&pid_0000#naoexiste#{dee824ef-729b-4a0e-9c14-b7117d33a817}";
    olho::ios::connection conn;
    std::string error;
    const bool opened = conn.open(fake, error);
    std::printf("abrir caminho inexistente: %s (%s)\n", opened ? "ABRIU (?!?)" : "recusado",
                error.c_str());

    std::printf("%s\n", (first.ok && !opened) ? "USB OK" : "USB FALHOU");
    return (first.ok && !opened) ? 0 : 1;
}