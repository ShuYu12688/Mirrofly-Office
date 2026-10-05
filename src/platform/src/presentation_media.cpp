#include <mirrorfly/presentation_media.hpp>

#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d11_4.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfmediaengine.h>
#include <mfobjects.h>
#include <windows.h>
#include <wrl/client.h>

namespace
{
    using Microsoft::WRL::ComPtr;

    std::string error_code(const char* message, HRESULT code)
    {
        std::ostringstream text;
        text << message << " (0x" << std::hex << static_cast<unsigned long>(code) << ")";
        return text.str();
    }

    struct MediaRuntime
    {
        HRESULT result = MFStartup(MF_VERSION);
        ~MediaRuntime()
        {
            if (SUCCEEDED(result))
                MFShutdown();
        }
    };

    struct Events
    {
        std::atomic<bool> ready{false};
        std::atomic<bool> ended{false};
        std::atomic<HRESULT> error{S_OK};
    };

    class MediaNotify final : public IMFMediaEngineNotify
    {
    public:
        explicit MediaNotify(std::shared_ptr<Events> events) : events_(std::move(events))
        {
        }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override
        {
            if (!result)
                return E_POINTER;
            *result = nullptr;
            if (id == __uuidof(IUnknown) || id == __uuidof(IMFMediaEngineNotify))
            {
                *result = static_cast<IMFMediaEngineNotify*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++references_;
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto count = --references_;
            if (!count)
                delete this;
            return count;
        }
        HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR first, DWORD second) override
        {
            if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE)
                SetEvent(reinterpret_cast<HANDLE>(first));
            else if (event == MF_MEDIA_ENGINE_EVENT_CANPLAY || event == MF_MEDIA_ENGINE_EVENT_LOADEDDATA)
                events_->ready = true;
            else if (event == MF_MEDIA_ENGINE_EVENT_ENDED)
                events_->ended = true;
            else if (event == MF_MEDIA_ENGINE_EVENT_PLAYING)
                events_->ended = false;
            else if (event == MF_MEDIA_ENGINE_EVENT_ERROR)
                events_->error = FAILED(static_cast<HRESULT>(second)) ? static_cast<HRESULT>(second) : E_FAIL;
            return S_OK;
        }

    private:
        std::atomic<ULONG> references_{1};
        std::shared_ptr<Events> events_;
    };
}

namespace mirrorfly
{
    struct PresentationMediaPlayer
    {
        const std::thread::id owner = std::this_thread::get_id();
        bool com_initialized = false;
        std::shared_ptr<Events> events = std::make_shared<Events>();
        ComPtr<IMFMediaEngine> engine;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IMFDXGIDeviceManager> manager;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11Texture2D> readable;
        int width = 0;
        int height = 0;
        ~PresentationMediaPlayer()
        {
            if (engine)
                engine->Shutdown();
            engine.Reset();
            readable.Reset();
            texture.Reset();
            manager.Reset();
            context.Reset();
            device.Reset();
            if (com_initialized)
                CoUninitialize();
        }
        bool current_thread() const
        {
            return owner == std::this_thread::get_id();
        }
    };

    PresentationMediaOpenResult open_presentation_media(const std::string& path)
    {
        const auto file = QFileInfo(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())));
        if (path.find('\0') != std::string::npos || !file.isFile() || file.size() <= 0 ||
            file.size() > 512LL * 1024 * 1024 || file.absoluteFilePath().startsWith("//"))
            return {{}, "媒体必须是有效的本地文件，大小不超过 512 MiB。"};
        auto player = std::make_shared<PresentationMediaPlayer>();
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        player->com_initialized = SUCCEEDED(com);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            return {{}, error_code("无法初始化媒体线程", com)};
        static MediaRuntime runtime;
        if (FAILED(runtime.result))
            return {{}, error_code("系统媒体功能不可用", runtime.result)};
        UINT token = 0;
        auto code = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, &player->device, nullptr, &player->context);
        if (FAILED(code))
            code = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
                D3D11_SDK_VERSION, &player->device, nullptr, &player->context);
        if (FAILED(code))
            return {{}, error_code("无法初始化视频绘制设备", code)};
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(player->context.As(&multithread)))
            multithread->SetMultithreadProtected(TRUE);
        code = MFCreateDXGIDeviceManager(&token, &player->manager);
        if (SUCCEEDED(code))
            code = player->manager->ResetDevice(player->device.Get(), token);
        if (FAILED(code))
            return {{}, error_code("无法初始化视频设备管理器", code)};
        ComPtr<IMFAttributes> attributes;
        code = MFCreateAttributes(&attributes, 4);
        if (FAILED(code))
            return {{}, error_code("无法初始化媒体属性", code)};
        ComPtr<IMFMediaEngineNotify> callback;
        callback.Attach(new MediaNotify(player->events));
        code = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, callback.Get());
        if (SUCCEEDED(code))
            code = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, player->manager.Get());
        if (SUCCEEDED(code))
            code = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        ComPtr<IMFMediaEngineClassFactory> factory;
        if (SUCCEEDED(code))
            code = CoCreateInstance(
                CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(code))
            code = factory->CreateInstance(0, attributes.Get(), &player->engine);
        if (FAILED(code))
            return {{}, error_code("系统媒体播放器不可用", code)};
        const auto name = file.absoluteFilePath().toStdWString();
        BSTR source = SysAllocStringLen(name.data(), static_cast<UINT>(name.size()));
        if (!source)
            return {{}, "没有足够内存打开媒体。"};
        code = player->engine->SetSource(source);
        SysFreeString(source);
        if (FAILED(code))
            return {{}, error_code("无法打开媒体", code)};
        return {player, {}};
    }

    PresentationMediaState presentation_media_state(const PresentationMediaPlayerPtr& player)
    {
        PresentationMediaState result;
        if (!player || !player->current_thread())
        {
            result.error = "媒体播放器未就绪或调用线程无效。";
            return result;
        }
        result.ready = player->events->ready;
        result.ended = player->events->ended;
        result.playing = !player->engine->IsPaused() && !result.ended;
        result.has_video = player->engine->HasVideo();
        const auto position = player->engine->GetCurrentTime(), duration = player->engine->GetDuration();
        result.position = std::isfinite(position) ? std::max(0.0, position) : 0;
        result.duration = std::isfinite(duration) ? std::max(0.0, duration) : 0;
        DWORD width = 0, height = 0;
        if (SUCCEEDED(player->engine->GetNativeVideoSize(&width, &height)))
        {
            result.width = static_cast<int>(std::min<DWORD>(width, 32768));
            result.height = static_cast<int>(std::min<DWORD>(height, 32768));
        }
        if (FAILED(player->events->error.load()))
            result.error = error_code("媒体格式无法解码或播放失败", player->events->error);
        return result;
    }

    PresentationMediaFrame presentation_media_frame(const PresentationMediaPlayerPtr& player)
    {
        PresentationMediaFrame result;
        if (!player || !player->current_thread())
            return result;
        LONGLONG timestamp = 0;
        if (player->engine->OnVideoStreamTick(&timestamp) != S_OK)
            return result;
        const auto state = presentation_media_state(player);
        if (!state.has_video || state.width <= 0 || state.height <= 0)
            return result;
        const double scale = std::min({1.0, 1920.0 / state.width, 1080.0 / state.height});
        const int width = std::max(1, static_cast<int>(state.width * scale));
        const int height = std::max(1, static_cast<int>(state.height * scale));
        HRESULT code = S_OK;
        if (!player->texture || player->width != width || player->height != height)
        {
            player->texture.Reset();
            player->readable.Reset();
            D3D11_TEXTURE2D_DESC descriptor{};
            descriptor.Width = static_cast<UINT>(width);
            descriptor.Height = static_cast<UINT>(height);
            descriptor.MipLevels = descriptor.ArraySize = 1;
            descriptor.SampleDesc.Count = 1;
            descriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            descriptor.Usage = D3D11_USAGE_DEFAULT;
            descriptor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            code = player->device->CreateTexture2D(&descriptor, nullptr, &player->texture);
            descriptor.Usage = D3D11_USAGE_STAGING;
            descriptor.BindFlags = 0;
            descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(code))
                code = player->device->CreateTexture2D(&descriptor, nullptr, &player->readable);
            if (FAILED(code))
            {
                player->events->error = code;
                return result;
            }
            player->width = width;
            player->height = height;
        }
        MFVideoNormalizedRect source{0, 0, 1, 1};
        RECT destination{0, 0, width, height};
        MFARGB border{0, 0, 0, 255};
        code = player->engine->TransferVideoFrame(player->texture.Get(), &source, &destination, &border);
        if (FAILED(code))
        {
            player->events->error = code;
            return result;
        }
        player->context->CopyResource(player->readable.Get(), player->texture.Get());
        D3D11_MAPPED_SUBRESOURCE pixels{};
        result.bgra.resize(static_cast<std::size_t>(width) * height * 4);
        code = player->context->Map(player->readable.Get(), 0, D3D11_MAP_READ, 0, &pixels);
        if (FAILED(code))
        {
            player->events->error = code;
            return {};
        }
        result.width = width;
        result.height = height;
        for (int row = 0; row < height; ++row)
            std::memcpy(result.bgra.data() + static_cast<std::size_t>(row) * width * 4,
                static_cast<const std::uint8_t*>(pixels.pData) +
                    static_cast<std::size_t>(row) * pixels.RowPitch,
                static_cast<std::size_t>(width) * 4);
        player->context->Unmap(player->readable.Get(), 0);
        return result;
    }

    bool play_presentation_media(const PresentationMediaPlayerPtr& player, bool playing)
    {
        if (!player || !player->current_thread())
            return false;
        return SUCCEEDED(playing ? player->engine->Play() : player->engine->Pause());
    }

    bool seek_presentation_media(const PresentationMediaPlayerPtr& player, double seconds)
    {
        if (!player || !player->current_thread() || !std::isfinite(seconds) || seconds < 0 ||
            seconds > presentation_media_state(player).duration)
            return false;
        player->events->ended = false;
        return SUCCEEDED(player->engine->SetCurrentTime(seconds));
    }

    bool configure_presentation_media(const PresentationMediaPlayerPtr& player, double volume, bool loop)
    {
        return player && player->current_thread() && std::isfinite(volume) && volume >= 0 && volume <= 1 &&
            SUCCEEDED(player->engine->SetVolume(volume)) && SUCCEEDED(player->engine->SetLoop(loop));
    }
}
#else
namespace mirrorfly
{
    struct PresentationMediaPlayer
    {
    };
    PresentationMediaOpenResult open_presentation_media(const std::string&)
    {
        return {{}, "当前平台尚未配置媒体播放后端。"};
    }
    PresentationMediaState presentation_media_state(const PresentationMediaPlayerPtr&)
    {
        PresentationMediaState result;
        result.error = "当前平台尚未配置媒体播放后端。";
        return result;
    }
    PresentationMediaFrame presentation_media_frame(const PresentationMediaPlayerPtr&)
    {
        return {};
    }
    bool play_presentation_media(const PresentationMediaPlayerPtr&, bool)
    {
        return false;
    }
    bool seek_presentation_media(const PresentationMediaPlayerPtr&, double)
    {
        return false;
    }
    bool configure_presentation_media(const PresentationMediaPlayerPtr&, double, bool)
    {
        return false;
    }
}
#endif
