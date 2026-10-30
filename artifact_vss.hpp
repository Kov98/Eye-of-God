#pragma once

#include "artifacts.hpp"

#include <string>

namespace deep {

int parse_vss_text(const std::string& text, const ArtifactSink& sink,
                   std::wstring& error);
int collect_vss(const ArtifactSink& sink, std::wstring& error);

} // namespace deep
