#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mirrorfly
{

    enum class DocumentKind
    {
        Writer,
        Sheets,
        Slides,
        Other,
        Pdf,
        Mindmap
    };

    struct RecentFile
    {
        std::string path;
        std::string name;
        std::string modified;
        std::string size_text;
        DocumentKind kind = DocumentKind::Other;
        bool starred = false;
    };

    DocumentKind classify_document(const std::string& path);

    // Categories: all, recent, writer, sheets, slides, other, starred.
    std::vector<RecentFile> filter_files(
        const std::vector<RecentFile>& files, const std::string& query, const std::string& category);

    // Keep the newest metadata and preserve any existing star for the same path.
    std::vector<RecentFile> remember_file(
        const std::vector<RecentFile>& files, const RecentFile& file, std::size_t limit = 30);

    std::vector<RecentFile> toggle_star(const std::vector<RecentFile>& files, const std::string& path);

    std::string document_kind_key(DocumentKind kind);

}
