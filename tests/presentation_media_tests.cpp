#include "presentation_media_fixture.hpp"
#include <mirrorfly/presentation_media.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <cmath>
#include <iostream>

namespace
{
    using Microsoft::WRL::ComPtr;
    bool check(bool condition, const char* text)
    {
        if (!condition)
            std::cerr << text << '\n';
        return condition;
    }

    bool verify_player(const QString& file)
    {
        using namespace mirrorfly;
        const auto opened = open_presentation_media(file.toUtf8().toStdString());
        if (!check(opened.player != nullptr, opened.error.c_str()))
            return false;
        bool passed = check(configure_presentation_media(opened.player, 0, false), "silent test playback");
        passed = check(!configure_presentation_media(opened.player, 2, false), "invalid volume rejected") &&
            passed;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 6000 && !presentation_media_state(opened.player).ready &&
            presentation_media_state(opened.player).error.empty())
        {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        auto state = presentation_media_state(opened.player);
        if (!state.error.empty())
            std::cerr << state.error << '\n';
        passed = check(state.ready && state.has_video && state.width == 64 && state.height == 64 &&
                         state.duration > 1.8,
                     "real H264 metadata becomes ready without a window") &&
            passed;
        passed = check(play_presentation_media(opened.player, true), "start video") && passed;
        bool red = false, blue = false;
        timer.restart();
        while (timer.elapsed() < 4000 && !(red && blue))
        {
            QCoreApplication::processEvents();
            const auto frame = presentation_media_frame(opened.player);
            if (!frame.bgra.empty())
            {
                const auto pixel = (frame.width * (frame.height / 2) + frame.width / 2) * 4;
                red = red || (frame.bgra[pixel + 2] > 150 && frame.bgra[pixel] < 60);
                blue = blue || (frame.bgra[pixel] > 150 && frame.bgra[pixel + 2] < 60);
            }
            state = presentation_media_state(opened.player);
            if (!state.error.empty())
            {
                std::cerr << state.error << '\n';
                break;
            }
            QThread::msleep(10);
        }
        passed = check(red && blue && state.position > .8, "decoded video colors and media clock progress") &&
            passed;
        passed = check(play_presentation_media(opened.player, false), "pause video") && passed;
        passed = check(seek_presentation_media(opened.player, .2), "seek video") && passed;
        passed = check(!seek_presentation_media(opened.player, -1), "negative seek rejected") && passed;
        return passed;
    }
}

int run_presentation_media_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    const auto media = MFStartup(MF_VERSION);
    QTemporaryDir directory;
    const auto file = directory.filePath("colors.mp4");
    const auto created = SUCCEEDED(media) ? mirrorfly::test_fixture::write_video(file) : media;
    bool passed = check(SUCCEEDED(created), "generate H264 video fixture with system encoder");
    if (FAILED(created))
        std::cerr << std::hex << created << '\n';
    if (passed)
        passed = verify_player(file);
    if (SUCCEEDED(media))
        MFShutdown();
    CoUninitialize();
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_media_tests(argc, argv);
}
