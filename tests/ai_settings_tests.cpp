#include "office_ai_agent.hpp"
#include "office_ai_settings.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

int run_ai_settings_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    const QString path = directory.filePath("ai-model.dat");
    mirrorfly::OfficeAiSettings settings(path);
    mirrorfly::OfficeAiAgent agent(
        nullptr, nullptr, directory.filePath("test.jsonl"), directory.path(), &settings);
    bool passed = check(!agent.configured(), "an absent profile requires configuration");
    passed = check(agent.configure("https://api.deepseek.com", "deepseek-flash", "fake-test-secret", "low"),
                 "a valid model configuration is saved") &&
        passed;
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    const QByteArray encrypted = file.readAll();
    file.close();
    passed = check(!encrypted.isEmpty() && !encrypted.contains("fake-test-secret") &&
                     !encrypted.contains("deepseek-flash"),
                 "credentials and model fields are protected on disk") &&
        passed;
    passed = check(agent.configure("https://api.deepseek.com", "deepseek-reasoner", "", "low") &&
                     settings.configuration().key == "fake-test-secret",
                 "leaving the key empty preserves it while updating the model") &&
        passed;
    passed = check(agent.setThinkingEffort("high") && agent.thinkingEffort() == "high" &&
                     !agent.setThinkingEffort("invalid"),
                 "thinking effort is validated and persisted") &&
        passed;
    mirrorfly::OfficeAiSettings restored_settings(path);
    mirrorfly::OfficeAiAgent restored(
        nullptr, nullptr, directory.filePath("restored.jsonl"), directory.path(), &restored_settings);
    passed = check(restored.configured() && restored.modelName() == "deepseek-reasoner" &&
                     restored.thinkingEffort() == "high" &&
                     restored_settings.configuration().key == "fake-test-secret",
                 "another session restores the same model, key and thinking effort") &&
        passed;
    passed = check(!agent.configure("http://invalid.example", "model", "replacement", "none") &&
                     settings.configuration().key == "fake-test-secret",
                 "invalid replacement leaves the saved configuration intact") &&
        passed;
    passed = check(agent.configure("https://api.deepseek.com", "deepseek-flash", "new-fake-secret", "none"),
                 "a new key replaces the previous configuration") &&
        passed;
    mirrorfly::OfficeAiSettings replaced(path);
    passed = check(replaced.load() && replaced.configuration().key == "new-fake-secret",
                 "the replacement key is restored") &&
        passed;
    mirrorfly::OfficeAiSettings unavailable(directory.path());
    passed = check(!agent.configure("https://custom.example/v1", "custom-model", "", "none") &&
                     settings.configuration().key == "new-fake-secret",
                 "switching services cannot forward a saved key without explicit replacement") &&
        passed;
    passed = check(agent.configure("https://custom.example/v1", "custom-model", "custom-fake-key", "none"),
                 "custom endpoints can be saved with their own credentials") &&
        passed;
    mirrorfly::OfficeAiSettings custom(path);
    passed = check(custom.load() && custom.configuration().address == "https://custom.example/v1" &&
                     custom.configuration().key == "custom-fake-key",
                 "a custom endpoint and its encrypted credential survive restart") &&
        passed;
    passed =
        check(!unavailable.save(replaced.configuration()), "a failed atomic write is reported") && passed;
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write("damaged-profile");
    file.close();
    mirrorfly::OfficeAiSettings damaged(path);
    passed = check(!damaged.load() && damaged.configuration().key.isEmpty(),
                 "a damaged profile never yields credentials") &&
        passed;
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_ai_settings_tests(argc, argv);
}
