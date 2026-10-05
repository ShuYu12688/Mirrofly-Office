#pragma once

#include "mirrorfly/core.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mirrorfly
{

    // History is bounded to 30 records and a 1 MiB JSON file.
    std::vector<RecentFile> load_recent_files();
    bool save_recent_files(const std::vector<RecentFile>& files);
    std::optional<RecentFile> inspect_local_file(const std::string& path);

}
