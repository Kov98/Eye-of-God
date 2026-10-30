// language: C++, file: artifact_evtx.cpp, target: Windows 10/11
// Offline EVTX reader. Uses the Windows Event Log API against a file path, so
// exported and carved EVTX files can be parsed without a live channel.

#include "artifacts.hpp"

#include <winevt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "wevtapi.lib")

namespace deep {
namespace {

std::wstring find_tag_text(const std::wstring& xml, const wchar_t* name) {
    const std::wstring opening = std::wstring(L"<") + name;
    const std::size_t start = xml.find(opening);
    if (start == std::wstring::npos) return {};
    const std::size_t content = xml.find(L'>', start);
    if (content == std::wstring::npos) return {};
    const std::wstring closing = std::wstring(L"</") + name + L">";
    const std::size_t end = xml.find(closing, content + 1);
    if (end == std::wstring::npos) return {};
    return xml.substr(content + 1, end - content - 1);
}

std::wstring find_attribute(const std::wstring& xml, const wchar_t* element,
                            const wchar_t* attribute) {
    const std::wstring opening = std::wstring(L"<") + element;
    std::size_t start = xml.find(opening);
    if (start == std::wstring::npos) return {};
    const std::size_t tag_end = xml.find(L'>', start);
    if (tag_end == std::wstring::npos) return {};
    const std::wstring tag = xml.substr(start, tag_end - start + 1);
    const std::wstring prefix = std::wstring(attribute) + L"=";
    const std::size_t attr = tag.find(prefix);
    if (attr == std::wstring::npos) return {};
    std::size_t value_start = attr + prefix.size();
    if (value_start >= tag.size()) return {};
    const wchar_t quote = tag[value_start];
    if (quote != L'"' && quote != L'\'') return {};
    ++value_start;
    const std::size_t value_end = tag.find(quote, value_start);
    if (value_end == std::wstring::npos) return {};
    return tag.substr(value_start, value_end - value_start);
}

std::string decode_entities(std::string value) {
    const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}
    };
    for (const auto& entity : entities) {
        std::size_t position = 0;
        while ((position = value.find(entity.first, position)) != std::string::npos) {
            value.replace(position, std::strlen(entity.first), entity.second);
            position += std::strlen(entity.second);
        }
    }
    return value;
}

std::string event_data_json(const std::wstring& xml) {
    std::string out = "[";
    bool first = true;
    std::size_t position = 0;
    while (true) {
        const std::size_t start = xml.find(L"<Data", position);
        if (start == std::wstring::npos) break;
        const std::size_t tag_end = xml.find(L'>', start);
        if (tag_end == std::wstring::npos) break;
        const std::size_t close = xml.find(L"</Data>", tag_end + 1);
        if (close == std::wstring::npos) break;
        std::wstring name;
        std::size_t attr = xml.find(L" Name=", start);
        if (attr != std::wstring::npos && attr < tag_end) {
            std::size_t value_start = attr + 6;
            if (value_start < tag_end &&
                (xml[value_start] == L'"' || xml[value_start] == L'\'')) {
                const wchar_t quote = xml[value_start++];
                const std::size_t value_end = xml.find(quote, value_start);
                if (value_end != std::wstring::npos && value_end < tag_end)
                    name = xml.substr(value_start, value_end - value_start);
            }
        }
        const std::wstring value = xml.substr(tag_end + 1, close - tag_end - 1);
        if (!first) out += ",";
        first = false;
        out += "{\"name\":";
        out += art::json_escape_w(name);
        out += ",\"value\":";
        out += art::json_escape(decode_entities(art::to_utf8(value)));
        out += "}";
        position = close + 7;
    }
    out += "]";
    return out;
}

bool render_event(EVT_HANDLE event, std::wstring& xml, DWORD& code) {
    DWORD used = 0;
    DWORD property_count = 0;
    const BOOL size_query = EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr,
                                      &used, &property_count);
    code = size_query ? ERROR_SUCCESS : GetLastError();
    if (size_query || code != ERROR_INSUFFICIENT_BUFFER || used == 0) return false;
    std::vector<wchar_t> buffer((used + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
    if (!EvtRender(nullptr, event, EvtRenderEventXml,
                   static_cast<DWORD>(buffer.size() * sizeof(wchar_t)),
                   buffer.data(), &used, &property_count)) {
        code = GetLastError();
        return false;
    }
    xml.assign(buffer.data());
    return true;
}

} // namespace

int collect_evtx_file(const std::wstring& file, const ArtifactSink& sink,
                      std::wstring& error) {
    EVT_HANDLE query = EvtQuery(nullptr, file.c_str(), L"*",
        EvtQueryFilePath | EvtQueryReverseDirection);
    if (query == nullptr) {
        error = L"EvtQuery file failed, error " +
            std::to_wstring(GetLastError());
        return -1;
    }
    int rows = 0;
    while (true) {
        EVT_HANDLE events[32] = {};
        DWORD returned = 0;
        if (!EvtNext(query, 32, events, 0, 0, &returned)) {
            const DWORD code = GetLastError();
            if (code != ERROR_NO_MORE_ITEMS) {
                error = L"EvtNext failed, error " + std::to_wstring(code);
            }
            break;
        }
        for (DWORD i = 0; i < returned; ++i) {
            std::wstring xml;
            DWORD code = ERROR_SUCCESS;
            if (render_event(events[i], xml, code)) {
                const std::wstring id = find_tag_text(xml, L"EventID");
                const std::wstring channel = find_tag_text(xml, L"Channel");
                const std::wstring record = find_tag_text(xml, L"EventRecordID");
                const std::wstring computer = find_tag_text(xml, L"Computer");
                const std::wstring provider =
                    find_attribute(xml, L"Provider", L"Name");
                const std::wstring timestamp =
                    find_attribute(xml, L"TimeCreated", L"SystemTime");
                ArtifactRow row;
                row.type = "evtx";
                row.source = art::to_utf8(file);
                row.key = art::to_utf8(record);
                row.timestamp_utc = art::to_utf8(timestamp);
                row.path = art::to_utf8(file);
                row.name = "EventID " + art::to_utf8(id) + " / " +
                    art::to_utf8(provider);
                std::string data = "{\"event_id\":" + art::json_escape_w(id) +
                    ",\"channel\":" + art::json_escape_w(channel) +
                    ",\"record_id\":" + art::json_escape_w(record) +
                    ",\"computer\":" + art::json_escape_w(computer) +
                    ",\"provider\":" + art::json_escape_w(provider) +
                    ",\"event_data\":" + event_data_json(xml) + "}";
                if (data.size() > 256 * 1024) data.resize(256 * 1024);
                row.data = std::move(data);
                sink(row);
                ++rows;
            }
            EvtClose(events[i]);
        }
    }
    EvtClose(query);
    return rows;
}

int collect_evtx_directory(const std::wstring& directory,
                           const ArtifactSink& sink, std::wstring& error) {
    int total = 0;
    std::vector<std::wstring> pending{directory};
    while (!pending.empty()) {
        const std::wstring current = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW entry{};
        const std::wstring pattern = art::join_path(current, L"*");
        HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            const std::wstring path = art::join_path(current, entry.cFileName);
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    pending.push_back(path);
                continue;
            }
            std::wstring lower = path;
            for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
            if (lower.size() < 5 || lower.substr(lower.size() - 5) != L".evtx")
                continue;
            std::wstring file_error;
            const int count = collect_evtx_file(path, sink, file_error);
            if (count > 0) total += count;
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    return total;
}

} // namespace deep
