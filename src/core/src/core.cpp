#include "mirrorfly/core.hpp"

#include <algorithm>
#include <unordered_set>

namespace
{

    std::string lowercase_ascii(std::string value)
    {
        for (char& character : value)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }

        return value;
    }

    bool matches_category(const mirrorfly::RecentFile& file, const std::string& category)
    {
        if (category.empty() || category == "all" || category == "recent")
        {
            return true;
        }

        if (category == "starred")
        {
            return file.starred;
        }

        return mirrorfly::document_kind_key(file.kind) == category;
    }

}

namespace mirrorfly
{

    DocumentKind classify_document(const std::string& path)
    {
        const std::size_t separator = path.find_last_of("/\\");
        const std::size_t dot = path.find_last_of('.');

        if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
        {
            return DocumentKind::Other;
        }

        const std::string extension = lowercase_ascii(path.substr(dot + 1));
        if (extension == "pdf")
            return DocumentKind::Pdf;
        if (extension == "mfg")
            return DocumentKind::Mindmap;

        if (extension == "doc" || extension == "docx" || extension == "docm" || extension == "odt" ||
            extension == "rtf" || extension == "txt" || extension == "text" || extension == "md" ||
            extension == "markdown")
        {
            return DocumentKind::Writer;
        }

        if (extension == "xls" || extension == "xlsx" || extension == "xlsm" || extension == "ods" ||
            extension == "csv" || extension == "tsv")
        {
            return DocumentKind::Sheets;
        }

        if (extension == "ppt" || extension == "pptx" || extension == "pptm" || extension == "odp")
        {
            return DocumentKind::Slides;
        }

        return DocumentKind::Other;
    }

    std::vector<RecentFile> filter_files(
        const std::vector<RecentFile>& files, const std::string& query, const std::string& category)
    {
        const std::string normalized_query = lowercase_ascii(query);
        const std::string normalized_category = lowercase_ascii(category);
        std::vector<RecentFile> filtered;
        filtered.reserve(files.size());

        for (const RecentFile& file : files)
        {
            if (!matches_category(file, normalized_category))
            {
                continue;
            }

            if (normalized_query.empty() ||
                lowercase_ascii(file.name).find(normalized_query) != std::string::npos ||
                lowercase_ascii(file.path).find(normalized_query) != std::string::npos)
            {
                filtered.push_back(file);
            }
        }

        return filtered;
    }

    std::vector<RecentFile> remember_file(
        const std::vector<RecentFile>& files, const RecentFile& file, std::size_t limit)
    {
        if (limit == 0)
        {
            return {};
        }

        RecentFile newest = file;
        std::unordered_set<std::string> starred_paths;

        for (const RecentFile& existing : files)
        {
            if (existing.starred)
            {
                starred_paths.insert(existing.path);
            }
        }

        newest.starred = newest.starred || starred_paths.count(newest.path) != 0;
        std::vector<RecentFile> updated;
        updated.reserve(std::min(limit, files.size() + 1));
        updated.push_back(newest);
        std::unordered_set<std::string> seen_paths = {newest.path};

        for (const RecentFile& existing : files)
        {
            if (updated.size() >= limit)
            {
                break;
            }

            if (seen_paths.insert(existing.path).second)
            {
                updated.push_back(existing);
                updated.back().starred = starred_paths.count(existing.path) != 0;
            }
        }

        return updated;
    }

    std::vector<RecentFile> toggle_star(const std::vector<RecentFile>& files, const std::string& path)
    {
        std::vector<RecentFile> updated = files;

        for (RecentFile& file : updated)
        {
            if (file.path == path)
            {
                file.starred = !file.starred;
            }
        }

        return updated;
    }

    std::string document_kind_key(DocumentKind kind)
    {
        switch (kind)
        {
        case DocumentKind::Writer:
            return "writer";
        case DocumentKind::Sheets:
            return "sheets";
        case DocumentKind::Slides:
            return "slides";
        case DocumentKind::Pdf:
            return "pdf";
        case DocumentKind::Mindmap:
            return "mindmap";
        case DocumentKind::Other:
            return "other";
        }

        return "other";
    }

}
