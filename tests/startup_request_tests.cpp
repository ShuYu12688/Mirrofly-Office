#include "startup_request.hpp"

#include <QCoreApplication>
#include <QDir>
#include <iostream>

int run_startup_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    const auto check = [&](bool passed, const char* message)
    {
        if (!passed)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    const auto path = QDir::current().filePath(QStringLiteral("中文 空格 & % 文件.mfg"));
    const auto request = mirrorfly::startup_request({"MirrorflyOffice.exe", "--open", path});
    check(request.error.isEmpty() && request.file.toLocalFile() == path,
        "shell path preserves Unicode and metacharacters");
    check(mirrorfly::startup_request({"app", QUrl::fromLocalFile(path).toString()}).file == request.file,
        "file URL reaches the same target");
    check(!mirrorfly::startup_request({"app", "--open"}).error.isEmpty(), "missing open argument rejected");
    check(!mirrorfly::startup_request({"app", "old.mm"}).error.isEmpty(),
        "legacy extension rejected at startup");
    check(!mirrorfly::startup_request({"app", "https://example.invalid/a.pdf"}).error.isEmpty(),
        "remote URL rejected");
    check(!mirrorfly::startup_request({"app", "a.txt", "b.txt"}).error.isEmpty(),
        "multiple files do not silently discard input");
    for (const auto* suffix : {"txt", "text", "md", "markdown", "docx", "xlsx", "pptx", "pdf", "mfg"})
        check(mirrorfly::startup_request({"app", "--test-session", QString("file.%1").arg(suffix)})
                  .error.isEmpty(),
            "registered extension accepted");
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_startup_tests(argc, argv);
}
