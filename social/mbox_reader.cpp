// language: C++, file: social/mbox_reader.cpp
#include "social/mbox_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace olho::social {
namespace {

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string trim(const std::string& value) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' ||
                           value[start] == '\n')) {
        ++start;
    }
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Latin-1 para UTF-8: um byte vira um ou dois bytes. Usado quando o cabecalho
// declara iso-8859-1, que aparece em export antigo.
std::string latin1_to_utf8(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Converte texto do charset declarado para UTF-8 (so os que aparecem em export).
std::string to_utf8(const std::string& value, const std::string& charset) {
    const std::string name = lower(charset);
    if (name.empty() || name == "utf-8" || name == "utf8" || name == "us-ascii" ||
        name == "ascii") {
        return value;
    }
    if (name == "iso-8859-1" || name == "latin1" || name == "windows-1252") {
        return latin1_to_utf8(value);
    }
    return value;   // charset desconhecido: preserva os bytes como vieram
}

bool is_multipart(const std::string& content_type, std::string& boundary) {
    const std::string lowered = lower(content_type);
    if (lowered.rfind("multipart/", 0) != 0) return false;
    const std::size_t at = lowered.find("boundary=");
    if (at == std::string::npos) return false;
    std::string value = content_type.substr(at + 9);
    value = trim(value);
    if (!value.empty() && value.front() == '"') {
        const std::size_t close = value.find('"', 1);
        boundary = value.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    } else {
        const std::size_t semicolon = value.find(';');
        boundary = trim(value.substr(0, semicolon));
    }
    return !boundary.empty();
}

struct part_view {
    std::string headers;
    std::string body;
};

void split_headers_body(const std::string& block, part_view& out) {
    const std::size_t split = block.find("\r\n\r\n");
    const std::size_t alt = block.find("\n\n");
    std::size_t at = std::string::npos;
    std::size_t width = 2;
    if (split != std::string::npos && (alt == std::string::npos || split <= alt)) {
        at = split;
        width = 4;
    } else if (alt != std::string::npos) {
        at = alt;
        width = 2;
    }
    if (at == std::string::npos) {
        out.headers = block;
        out.body.clear();
        return;
    }
    out.headers = block.substr(0, at);
    out.body = block.substr(at + width);
}

std::string header_value(const std::string& headers, const std::string& name) {
    const std::string wanted = lower(name) + ":";
    std::size_t pos = 0;
    std::string result;
    while (pos < headers.size()) {
        std::size_t end = headers.find('\n', pos);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Continuacao de cabecalho: linha comecando por espaco/tab pertence ao anterior.
        if (!line.empty() && (line.front() == ' ' || line.front() == '\t') && !result.empty()) {
            result += " " + trim(line);
        } else if (lower(line).rfind(wanted, 0) == 0) {
            result = trim(line.substr(wanted.size()));
        } else if (!result.empty()) {
            break;
        }
        pos = end + 1;
    }
    return result;
}

std::string decode_transfer(const std::string& body, const std::string& encoding) {
    const std::string name = lower(trim(encoding));
    if (name == "base64") return decode_base64(body);
    if (name == "quoted-printable") return decode_quoted_printable(body);
    return body;
}

}  // namespace

std::string decode_base64(const std::string& value) {
    std::string out;
    out.reserve(value.size() * 3 / 4);
    int buffer = 0;
    int bits = 0;
    for (char c : value) {
        if (c == '=') break;
        const int v = base64_value(c);
        if (v < 0) continue;   // ignora espaco, quebra de linha e lixo
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string decode_quoted_printable(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '=' && i + 1 < value.size()) {
            if (value[i + 1] == '\n') {          // quebra suave: some
                i += 1;
                continue;
            }
            if (value[i + 1] == '\r' && i + 2 < value.size() && value[i + 2] == '\n') {
                i += 2;
                continue;
            }
            const int hi = hex_value(value[i + 1]);
            const int lo = (i + 2 < value.size()) ? hex_value(value[i + 2]) : -1;
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string decode_rfc2047(const std::string& value) {
    std::string out;
    std::size_t pos = 0;
    while (pos < value.size()) {
        const std::size_t start = value.find("=?", pos);
        if (start == std::string::npos) {
            out += value.substr(pos);
            break;
        }
        out += value.substr(pos, start - pos);
        const std::size_t charset_end = value.find('?', start + 2);
        const std::size_t encoding_end =
            charset_end == std::string::npos ? std::string::npos : value.find('?', charset_end + 1);
        const std::size_t text_end =
            encoding_end == std::string::npos ? std::string::npos
                                              : value.find("?=", encoding_end + 1);
        if (text_end == std::string::npos) {
            out += value.substr(start);
            break;
        }
        const std::string charset = value.substr(start + 2, charset_end - start - 2);
        const std::string encoding =
            value.substr(charset_end + 1, encoding_end - charset_end - 1);
        const std::string text = value.substr(encoding_end + 1, text_end - encoding_end - 1);
        std::string decoded;
        if (lower(encoding) == "b") {
            decoded = decode_base64(text);
        } else if (lower(encoding) == "q") {
            std::string escaped;
            escaped.reserve(text.size());
            for (char c : text) {
                if (c == '_') {
                    escaped.push_back(' ');
                } else if (c == '=') {
                    escaped.push_back('=');
                } else {
                    escaped.push_back(c);
                }
            }
            decoded = decode_quoted_printable(escaped);
        } else {
            decoded = text;
        }
        out += to_utf8(decoded, charset);
        pos = text_end + 2;
    }
    return out;
}

std::string mail_date_to_utc(const std::string& raw) {
    // Formato do Gmail: "Thu, 14 Nov 2023 19:13:00 -0300"
    static const char* months[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                     "jul", "aug", "sep", "oct", "nov", "dec"};
    const std::string text = trim(raw);
    if (text.empty()) return {};
    const std::size_t comma = text.find(',');
    const std::string rest = comma == std::string::npos ? text : trim(text.substr(comma + 1));
    int day = 0, year = 0, hour = 0, minute = 0, second = 0, month = 0;
    char month_name[16] = {};
    if (std::sscanf(rest.c_str(), "%d %15s %d %d:%d:%d", &day, month_name, &year, &hour, &minute,
                    &second) != 6) {
        return {};
    }
    const std::string month_lower = lower(month_name);
    for (int i = 0; i < 12; ++i) {
        if (month_lower.rfind(months[i], 0) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == 0 || day < 1 || day > 31 || year < 1970) return {};

    int offset_minutes = 0;
    const std::size_t tail = rest.find_last_of("+-");
    if (tail != std::string::npos) {
        const std::string zone = rest.substr(tail + 1);
        if (zone.size() >= 4 && zone.size() <= 5) {
            const int zone_hours = std::atoi(zone.substr(0, 2).c_str());
            const int zone_minutes = std::atoi(zone.substr(2, 2).c_str());
            offset_minutes = zone_hours * 60 + zone_minutes;
            if (rest[tail] == '-') offset_minutes = -offset_minutes;
        }
    }

    // Dias desde a epoca (algoritmo de Howard Hinnant, valido para o calendario
    // gregoriano proletico), sem depender de mktime e do fuso da maquina.
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u +
                         static_cast<unsigned>(day - 1);
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const long long days = static_cast<long long>(era) * 146097LL + static_cast<long long>(doe) -
                           719468LL;
    long long seconds = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    seconds -= static_cast<long long>(offset_minutes) * 60LL;

    // De volta para data civil, agora em UTC.
    const long long utc_days = seconds >= 0 ? seconds / 86400 : -(((-seconds) + 86399) / 86400);
    long long remainder = seconds - utc_days * 86400;
    if (remainder < 0) remainder += 86400;
    const long long z = utc_days + 719468;
    const long long era2 = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long long doe2 = static_cast<unsigned long long>(z - era2 * 146097);
    const unsigned long long yoe2 = (doe2 - doe2 / 1460u + doe2 / 36524u - doe2 / 146096u) / 365u;
    const long long yy = static_cast<long long>(yoe2) + era2 * 400;
    const unsigned long long doy2 = doe2 - (365u * yoe2 + yoe2 / 4u - yoe2 / 100u);
    const unsigned long long mp = (5u * doy2 + 2u) / 153u;
    const unsigned long long dd = doy2 - (153u * mp + 2u) / 5u + 1u;
    const unsigned long long mm = mp < 10 ? mp + 3 : mp - 9;
    const long long final_year = yy + (mm <= 2 ? 1 : 0);
    const int final_hour = static_cast<int>(remainder / 3600);
    const int final_minute = static_cast<int>((remainder % 3600) / 60);
    const int final_second = static_cast<int>(remainder % 60);
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02llu-%02lluT%02d:%02d:%02dZ", final_year, mm, dd,
                  final_hour, final_minute, final_second);
    return std::string(buffer);
}

bool parse_mbox(const std::string& data, std::vector<mail_message>& out, mbox_stats& stats,
                std::string& error) {
    out.clear();
    stats = mbox_stats{};
    error.clear();
    if (data.empty()) {
        error = "mbox vazio";
        return false;
    }

    // Separador de mensagem: "From " no inicio de uma linha. O corpo escapa
    // ocorrencias com ">From ", que sao desescapadas.
    std::vector<std::size_t> starts;
    if (data.rfind("From ", 0) == 0) starts.push_back(0);
    for (std::size_t i = 1; i < data.size(); ++i) {
        if (data[i] != 'F') continue;
        if (data[i - 1] != '\n') continue;
        if (data.compare(i, 5, "From ") == 0) starts.push_back(i);
    }
    if (starts.empty()) {
        error = "nenhuma mensagem encontrada (sem linha From)";
        return false;
    }

    for (std::size_t index = 0; index < starts.size(); ++index) {
        const std::size_t begin = starts[index];
        const std::size_t end = index + 1 < starts.size() ? starts[index + 1] : data.size();
        std::string block = data.substr(begin, end - begin);
        // Remove a linha "From " do envelope, se presente.
        const std::size_t first_newline = block.find('\n');
        if (block.rfind("From ", 0) == 0 && first_newline != std::string::npos) {
            block = block.substr(first_newline + 1);
        }
        // Desescapa ">From " (mboxrd).
        std::string unescaped;
        unescaped.reserve(block.size());
        for (std::size_t i = 0; i < block.size(); ++i) {
            if (block.compare(i, 6, ">From ") == 0 && (i == 0 || block[i - 1] == '\n')) {
                unescaped.append("From ");
                i += 5;
                continue;
            }
            unescaped.push_back(block[i]);
        }

        part_view top;
        split_headers_body(unescaped, top);
        mail_message message;
        message.offset = begin;
        message.from = decode_rfc2047(header_value(top.headers, "From"));
        message.to = decode_rfc2047(header_value(top.headers, "To"));
        message.subject = decode_rfc2047(header_value(top.headers, "Subject"));
        message.date_raw = header_value(top.headers, "Date");
        message.message_id = trim(header_value(top.headers, "Message-ID"));
        message.date_utc = mail_date_to_utc(message.date_raw);

        const std::string content_type = header_value(top.headers, "Content-Type");
        const std::string encoding = header_value(top.headers, "Content-Transfer-Encoding");
        std::string boundary;
        if (is_multipart(content_type, boundary)) {
            // Escolhe a primeira parte text/plain; se so houver html, marca.
            const std::string marker = "--" + boundary;
            std::size_t cursor = 0;
            bool took_text = false;
            while (true) {
                const std::size_t part_start = unescaped.find(marker, cursor);
                if (part_start == std::string::npos) break;
                const std::size_t body_start = unescaped.find('\n', part_start);
                if (body_start == std::string::npos) break;
                const std::size_t next = unescaped.find(marker, body_start);
                const std::string part = unescaped.substr(
                    body_start + 1, next == std::string::npos ? std::string::npos
                                                              : next - body_start - 1);
                part_view view;
                split_headers_body(part, view);
                const std::string part_type = lower(header_value(view.headers, "Content-Type"));
                const std::string part_encoding = header_value(view.headers, "Content-Transfer-Encoding");
                if (part_type.rfind("text/plain", 0) == 0 && !took_text) {
                    message.body = decode_transfer(view.body, part_encoding);
                    took_text = true;
                } else if (part_type.rfind("text/html", 0) == 0 && message.body.empty()) {
                    message.html_only = true;
                }
                cursor = body_start;
                if (next == std::string::npos) break;
            }
            // Anexos: nome de arquivo declarado nas partes.
            std::size_t scan = 0;
            while (true) {
                const std::size_t name_at = unescaped.find("filename=", scan);
                if (name_at == std::string::npos) break;
                std::size_t value_at = name_at + 9;
                std::string name;
                if (value_at < unescaped.size() && unescaped[value_at] == '"') {
                    const std::size_t close = unescaped.find('"', value_at + 1);
                    if (close != std::string::npos) name = unescaped.substr(value_at + 1, close - value_at - 1);
                } else {
                    const std::size_t stop = unescaped.find_first_of(";\r\n", value_at);
                    name = unescaped.substr(value_at, stop == std::string::npos ? std::string::npos
                                                                               : stop - value_at);
                }
                if (!name.empty()) message.attachments.push_back(decode_rfc2047(trim(name)));
                scan = value_at;
            }
        } else if (lower(content_type).rfind("text/html", 0) == 0) {
            message.html_only = true;
        } else {
            message.body = decode_transfer(top.body, encoding);
        }

        // O separador "From " do mbox deixa a linha em branco que precede a
        // mensagem seguinte dentro do bloco; ela nao faz parte do corpo.
        while (!message.body.empty() &&
               (message.body.back() == '\n' || message.body.back() == '\r')) {
            message.body.pop_back();
        }
        if (message.body.empty() && message.html_only) ++stats.html_only;
        if (!message.date_utc.empty()) ++stats.with_date;
        if (!message.body.empty()) ++stats.with_body;
        if (message.body.empty() && !message.html_only) ++stats.undecodable;
        ++stats.messages;
        out.push_back(std::move(message));
    }
    return true;
}

}  // namespace olho::social
