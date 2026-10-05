#pragma once

#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>

namespace mirrorfly::test_fixture
{
    using Microsoft::WRL::ComPtr;
    inline HRESULT write_video(const QString& path)
    {
        ComPtr<IMFSinkWriter> writer;
        auto code = MFCreateSinkWriterFromURL(path.toStdWString().c_str(), nullptr, nullptr, &writer);
        ComPtr<IMFMediaType> output, input;
        DWORD stream = 0;
        if (SUCCEEDED(code))
            code = MFCreateMediaType(&output);
        if (SUCCEEDED(code))
            code = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(code))
            code = output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(code))
            code = output->SetUINT32(MF_MT_AVG_BITRATE, 200000);
        if (SUCCEEDED(code))
            code = output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(code))
            code = MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, 64, 64);
        if (SUCCEEDED(code))
            code = MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, 10, 1);
        if (SUCCEEDED(code))
            code = MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(code))
            code = writer->AddStream(output.Get(), &stream);
        if (SUCCEEDED(code))
            code = MFCreateMediaType(&input);
        if (SUCCEEDED(code))
            code = output->CopyAllItems(input.Get());
        if (SUCCEEDED(code))
            code = input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(code))
            code = writer->SetInputMediaType(stream, input.Get(), nullptr);
        if (SUCCEEDED(code))
            code = writer->BeginWriting();
        for (int frame = 0; frame < 20 && SUCCEEDED(code); ++frame)
        {
            ComPtr<IMFMediaBuffer> buffer;
            ComPtr<IMFSample> sample;
            code = MFCreateMemoryBuffer(64 * 64 * 4, &buffer);
            BYTE* bytes = nullptr;
            if (SUCCEEDED(code))
                code = buffer->Lock(&bytes, nullptr, nullptr);
            if (FAILED(code))
                break;
            for (int pixel = 0; pixel < 64 * 64; ++pixel)
            {
                bytes[pixel * 4] = frame < 10 ? 0 : 255;
                bytes[pixel * 4 + 1] = 0;
                bytes[pixel * 4 + 2] = frame < 10 ? 255 : 0;
                bytes[pixel * 4 + 3] = 255;
            }
            buffer->Unlock();
            code = buffer->SetCurrentLength(64 * 64 * 4);
            if (SUCCEEDED(code))
                code = MFCreateSample(&sample);
            if (SUCCEEDED(code))
                code = sample->AddBuffer(buffer.Get());
            if (SUCCEEDED(code))
                code = sample->SetSampleTime(frame * 1000000LL);
            if (SUCCEEDED(code))
                code = sample->SetSampleDuration(1000000);
            if (SUCCEEDED(code))
                code = writer->WriteSample(stream, sample.Get());
        }
        if (SUCCEEDED(code))
            code = writer->Finalize();
        return code;
    }

}
