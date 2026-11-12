// language: C++, file: .oracle/lockdown_test.cpp
#include "lockdown.hpp"

#include <cstdio>
#include <string>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

// QueryType real: o lockdownd devolve um Value cheio de campos.
static const char* kQueryType =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>"
    "<key>Label</key><string>OlhoDeDeus</string>"
    "<key>Value</key><dict>"
    "<key>UniqueDeviceID</key><string>00008101-001A2B3C4D5E6F70</string>"
    "<key>DeviceName</key><string>iPhone de Teste</string>"
    "<key>ProductType</key><string>iPhone12,1</string>"
    "<key>ProductVersion</key><string>17.4.1</string>"
    "<key>BuildVersion</key><string>21E236</string>"
    "<key>HardwareModel</key><string>N104AP</string>"
    "<key>CPUArchitecture</key><string>arm64e</string>"
    "<key>SerialNumber</key><string>F17ABCDEFGHI</string>"
    "<key>PasswordProtected</key><true/>"
    "</dict></dict></plist>";

static const char* kError =
    "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
    "<key>Error</key><string>InvalidHostID</string>"
    "<key>ErrorCode</key><integer>-3</integer>"
    "</dict></plist>";

static const char* kSessionSsl =
    "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
    "<key>Label</key><string>OlhoDeDeus</string>"
    "<key>Value</key><dict>"
    "<key>EnableSessionSSL</key><true/>"
    "<key>SessionID</key><string>8C1F2A9B4D</string>"
    "</dict></dict></plist>";

static const char* kSessionPlain =
    "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
    "<key>Value</key><dict>"
    "<key>EnableSessionSSL</key><false/>"
    "<key>SessionID</key><string>ABC123</string>"
    "</dict></dict></plist>";

int main() {
    // 1. requests: tem que conter Label e o Request certo
    {
        const std::string q = olho::lockdown::query_type_request("OlhoDeDeus");
        check("QueryType tem Request", q.find("<string>QueryType</string>") != std::string::npos);
        check("QueryType tem Label", q.find("OlhoDeDeus") != std::string::npos);
        check("QueryType fecha o plist", q.find("</plist>") != std::string::npos);

        const std::string s = olho::lockdown::start_session_request("OlhoDeDeus", "HOSTID-123");
        check("StartSession tem Request", s.find("<string>StartSession</string>") != std::string::npos);
        check("StartSession carrega HostID", s.find("HOSTID-123") != std::string::npos);

        const std::string stop = olho::lockdown::stop_session_request("OlhoDeDeus", "ABC123");
        check("StopSession carrega SessionID", stop.find("ABC123") != std::string::npos);

        // escaping: um label com & nao pode quebrar o XML
        const std::string risky = olho::lockdown::query_type_request("a&b<c>");
        check("label escapado", risky.find("a&amp;b&lt;c&gt;") != std::string::npos);
    }

    // 2. QueryType: todos os campos
    {
        olho::lockdown::device_info info;
        olho::lockdown::response st;
        check("parse QueryType", olho::lockdown::parse_device_info(kQueryType, info, st));
        check("UDID", info.unique_device_id == "00008101-001A2B3C4D5E6F70");
        check("nome com acento", info.device_name == "iPhone de Teste");
        check("ProductType", info.product_type == "iPhone12,1");
        check("iOS", info.product_version == "17.4.1");
        check("CPU", info.cpu_architecture == "arm64e");
        check("protegido por senha", info.password_protected);
        check("pareado por padrao", info.paired);
    }

    // 3. erro do lockdownd: tem que virar recusa, nao sucesso vazio
    {
        olho::lockdown::response st;
        check("erro recusado", !olho::lockdown::parse_reply(kError, st));
        check("mensagem do erro", st.error_text == "InvalidHostID");
        check("codigo do erro", st.error_code == -3);

        olho::lockdown::device_info info;
        check("device_info falha no erro", !olho::lockdown::parse_device_info(kError, info, st));
    }

    // 4. sessao: com e sem TLS
    {
        olho::lockdown::session s;
        olho::lockdown::response st;
        check("sessao com TLS", olho::lockdown::parse_session(kSessionSsl, s, st));
        check("session id", s.session_id == "8C1F2A9B4D");
        check("exige TLS", s.enable_ssl);

        olho::lockdown::session s2;
        check("sessao sem TLS", olho::lockdown::parse_session(kSessionPlain, s2, st));
        check("nao exige TLS", !s2.enable_ssl);
    }

    // 5. hostis
    {
        olho::lockdown::response st;
        check("lixo recusado", !olho::lockdown::parse_reply("nao e plist", st));
        check("vazio recusado", !olho::lockdown::parse_reply("", st));
        check("plist sem Value nem Error", !olho::lockdown::parse_device_info(
            "<?xml version=\"1.0\"?><plist><dict><key>X</key><string>y</string></dict></plist>",
            *new olho::lockdown::device_info(), st));

        olho::lockdown::session s;
        check("sessao sem SessionID recusada", !olho::lockdown::parse_session(
            "<?xml version=\"1.0\"?><plist><dict><key>Value</key><dict>"
            "<key>EnableSessionSSL</key><true/></dict></dict></plist>", s, st));
    }

    std::printf(failures == 0 ? "LOCKDOWN OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}