// NOTE: See https://stackoverflow.com/a/78051528/868014

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <mftransform.h>
#include <mfobjects.h>
#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <codecapi.h>

#include <unknwn.h>
#include <winrt\base.h>

#include <wil\com.h>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mf.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"wmcodecdspuuid.lib")

#pragma comment(lib,"dxguid.lib")

using namespace std::chrono;
using namespace std::chrono_literals;

struct Application
{
    void Run()
    {
        wil::com_ptr<ID3D11Device> device;
        D3D_FEATURE_LEVEL featureLevel;
        wil::com_ptr<ID3D11DeviceContext> context;
        THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, device.put(), &featureLevel, context.put()));
        auto const dxgiDevice = device.query<IDXGIDevice1>();
        wil::com_ptr<IDXGIAdapter> dxgiAdapter;
        THROW_IF_FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()));
        wil::com_ptr<IDXGIOutput> output;
        THROW_IF_FAILED(dxgiAdapter->EnumOutputs(0, output.put()));
        auto const output1 = output.query<IDXGIOutput1>();
        wil::com_ptr<IDXGIOutputDuplication> dxgiOutput;
        THROW_IF_FAILED(output1->DuplicateOutput(device.get(), dxgiOutput.put()));
        DXGI_OUTDUPL_DESC DxgiOutputDesc;
        dxgiOutput->GetDesc(&DxgiOutputDesc);
        _RPTWN(_CRT_WARN, L"DxgiOutputDesc.ModeDesc %u %u\n", DxgiOutputDesc.ModeDesc.Width, DxgiOutputDesc.ModeDesc.Height);

        transform = wil::CoCreateInstance<IMFTransform>(CLSID_CMSH264EncoderMFT);

        wil::com_ptr<IMFMediaType> outputMediaType;
        THROW_IF_FAILED(MFCreateMediaType(&outputMediaType));
        THROW_IF_FAILED(outputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        THROW_IF_FAILED(outputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        THROW_IF_FAILED(MFSetAttributeRatio(outputMediaType.get(), MF_MT_FRAME_RATE, 30u, 1u));
        THROW_IF_FAILED(MFSetAttributeSize(outputMediaType.get(), MF_MT_FRAME_SIZE, DxgiOutputDesc.ModeDesc.Width, DxgiOutputDesc.ModeDesc.Height));
        THROW_IF_FAILED(outputMediaType->SetUINT32(MF_MT_AVG_BITRATE, 10'000'000u));
        THROW_IF_FAILED(outputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        THROW_IF_FAILED(transform->SetOutputType(0, outputMediaType.get(), 0));

        for(DWORD TypeIndex = 0; ; TypeIndex++)
        {
            wil::com_ptr<IMFMediaType> mediaType;
            THROW_IF_FAILED(transform->GetInputAvailableType(0, TypeIndex, mediaType.put()));
            GUID Subtype;
            THROW_IF_FAILED(mediaType->GetGUID(MF_MT_SUBTYPE, &Subtype));
            if(Subtype == MFVideoFormat_NV12)
            {
                THROW_IF_FAILED(transform->SetInputType(0, mediaType.get(), 0));
                break;
            }
        }

        auto const CodecApi = transform.query<ICodecAPI>();
        wil::unique_variant LowLatencyMode;
        LowLatencyMode.vt = VT_BOOL;
        LowLatencyMode.boolVal = VARIANT_TRUE;
        THROW_IF_FAILED(CodecApi->SetValue(&CODECAPI_AVLowLatencyMode, &LowLatencyMode));

        THROW_IF_FAILED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        THROW_IF_FAILED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

        std::atomic_bool GetThreadTermination = false;
        std::thread GetThread([&] ()
        {
            std::ofstream OutputFile;
            OutputFile.open(L"C:\\Temp\\Output.h264", std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
            WI_ASSERT(!OutputFile.fail());
            MFT_OUTPUT_STREAM_INFO info;
            THROW_IF_FAILED(transform->GetOutputStreamInfo(0, &info));
            for(; !GetThreadTermination.load(); ) 
            {
                wil::com_ptr<IMFSample> sample;
                THROW_IF_FAILED(MFCreateSample(sample.put()));
                wil::com_ptr<IMFMediaBuffer> outputMediaBuffer;
                THROW_IF_FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(info.cbSize), outputMediaBuffer.put()));
                THROW_IF_FAILED(sample->AddBuffer(outputMediaBuffer.get()));
                MFT_OUTPUT_DATA_BUFFER outputDataBuffer { 0, sample.get() };
                DWORD status;
                auto const processOutputResult = transform->ProcessOutput(0, 1, &outputDataBuffer, &status);
                _RPTWN(_CRT_WARN, L"processOutputResult 0x%08X\n", processOutputResult);
                if(processOutputResult == MF_E_TRANSFORM_STREAM_CHANGE) 
                {
                    wil::com_ptr<IMFMediaType> mediaType;
                    THROW_IF_FAILED(transform->GetOutputAvailableType(0, 0, mediaType.put()));
                    THROW_IF_FAILED(transform->SetOutputType(0, mediaType.get(), 0));
                    GUID Subtype;
                    THROW_IF_FAILED(mediaType->GetGUID(MF_MT_SUBTYPE, &Subtype));
                    WI_ASSERT(Subtype == MFVideoFormat_NV12);
                    THROW_IF_FAILED(transform->GetOutputStreamInfo(0, &info));
                    continue;
                }
                if(processOutputResult == MF_E_TRANSFORM_NEED_MORE_INPUT) 
                {
                    std::this_thread::sleep_for(10ms);
                    continue;
                }
                THROW_IF_FAILED(processOutputResult);
                outputMediaBuffer.reset();
                THROW_IF_FAILED(sample->ConvertToContiguousBuffer(outputMediaBuffer.put()));
                BYTE* Data;
                DWORD DataCapacity, DataSize;
                THROW_IF_FAILED(outputMediaBuffer->Lock(&Data, &DataCapacity, &DataSize));
                auto&& DataScope = wil::scope_exit([&] () { THROW_IF_FAILED(outputMediaBuffer->Unlock()); });
                OutputFile.write(reinterpret_cast<char const*>(Data), DataSize);
                WI_ASSERT(!OutputFile.fail());
            }
        });

        UINT64 time = 0;
        UINT64 rtDuration = 0;
        THROW_IF_FAILED(MFFrameRateToAverageTimePerFrame(30, 1, &rtDuration));

        auto const videoDevice = device.query<ID3D11VideoDevice>();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc { };
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = DxgiOutputDesc.ModeDesc.Width;
        contentDesc.InputHeight = DxgiOutputDesc.ModeDesc.Height;
        contentDesc.OutputWidth = DxgiOutputDesc.ModeDesc.Width;
        contentDesc.OutputHeight = DxgiOutputDesc.ModeDesc.Height;
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        wil::com_ptr<ID3D11VideoProcessorEnumerator> vpe;
        THROW_IF_FAILED(videoDevice->CreateVideoProcessorEnumerator(&contentDesc, vpe.put()));
        D3D11_VIDEO_PROCESSOR_CAPS caps;
        THROW_IF_FAILED(vpe->GetVideoProcessorCaps(&caps));
        wil::com_ptr<ID3D11VideoProcessor> processor;
        bool stereo = caps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO;
        DXGI_FORMAT format1 = DXGI_FORMAT_B8G8R8A8_UNORM;
        DXGI_FORMAT format = DXGI_FORMAT_NV12;
        RECT src { 0, 0, static_cast<LONG>(contentDesc.InputWidth), static_cast<LONG>(contentDesc.InputHeight) };
        RECT dest { 0, 0, static_cast<LONG>(contentDesc.OutputWidth), static_cast<LONG>(contentDesc.OutputWidth) };

        DXGI_MODE_DESC1 modelFilter { };
        modelFilter.Width = DxgiOutputDesc.ModeDesc.Width;
        modelFilter.Height = DxgiOutputDesc.ModeDesc.Height;
        auto const videoContext = context.query<ID3D11VideoContext>();
        videoContext->VideoProcessorSetStreamFrameFormat(processor.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamOutputRate(processor.get(), 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
        RECT r1 = src;
        videoContext->VideoProcessorSetStreamSourceRect(processor.get(), 0, TRUE, &r1);
        videoContext->VideoProcessorSetStreamDestRect(processor.get(), 0, TRUE, &r1);
        videoContext->VideoProcessorSetOutputTargetRect(processor.get(), TRUE, &r1);
        
        D3D11_VIDEO_COLOR backgroundColor { };
        backgroundColor.RGBA.A = 1.0f;
        backgroundColor.RGBA.R = 1.0f;
        backgroundColor.RGBA.G = 1.0f;
        backgroundColor.RGBA.B = 1.0f;
        videoContext->VideoProcessorSetOutputBackgroundColor(processor.get(), FALSE, &backgroundColor);
        videoContext->VideoProcessorSetStreamOutputRate(processor.get(), 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);

        std::chrono::steady_clock Clock;
        auto const BaseTime = Clock.now();
        for(; ; ) 
        {
            if(Clock.now() - BaseTime >= 10s)
                break;

            DXGI_OUTDUPL_FRAME_INFO frameInfo;
            wil::com_ptr<IDXGIResource> resource;
            auto const AcquireNextFrameResult = dxgiOutput->AcquireNextFrame(1000u, &frameInfo, resource.put());
            _RPTWN(_CRT_WARN, L"processOutputResult 0x%08X\n", AcquireNextFrameResult);
            if(AcquireNextFrameResult == DXGI_ERROR_WAIT_TIMEOUT) 
                continue;
            THROW_IF_FAILED(AcquireNextFrameResult);
            auto const texture = wil::com_query<ID3D11Texture2D>(resource.get());

            CD3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc { D3D11_VPIV_DIMENSION_TEXTURE2D };
            wil::com_ptr<ID3D11VideoProcessorInputView> inputView;
            THROW_IF_FAILED(videoDevice->CreateVideoProcessorInputView(texture.get(), vpe.get(), &inputViewDesc, inputView.put()));

            desc.CPUAccessFlags = 0;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.MiscFlags = 0;
            desc.Format = DXGI_FORMAT_NV12;
            wil::com_ptr<ID3D11Texture2D> texture1;
            THROW_IF_FAILED(device->CreateTexture2D(&desc, nullptr, texture1.put()));
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc { D3D11_VPOV_DIMENSION_TEXTURE2D };
            wil::com_ptr<ID3D11VideoProcessorOutputView> outputView;
            THROW_IF_FAILED(videoDevice->CreateVideoProcessorOutputView(texture1.get(), vpe.get(), &outputViewDesc, outputView.put()));
            D3D11_VIDEO_PROCESSOR_STREAM st { TRUE };
            st.pInputSurface = inputView.get();
            THROW_IF_FAILED(videoContext->VideoProcessorBlt(processor.get(), outputView.get(), 0, 1, &st));

            dxgiOutput->ReleaseFrame();

            texture1->GetDesc(&desc);
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            auto const dxgiSurface = texture1.query<IDXGISurface>();

            wil::com_ptr<IMFMediaBuffer> buffer;
            THROW_IF_FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), dxgiSurface.get(), 0, FALSE, buffer.put()));
            DWORD Length;
            THROW_IF_FAILED(buffer.query<IMF2DBuffer>()->GetContiguousLength(&Length));
            THROW_IF_FAILED(buffer->SetCurrentLength(Length));
            wil::com_ptr<IMFSample> sample;
            THROW_IF_FAILED(MFCreateSample(sample.put()));
            THROW_IF_FAILED(sample->AddBuffer(buffer.get()));

            THROW_IF_FAILED(sample->SetSampleTime(time));
            THROW_IF_FAILED(sample->SetSampleDuration(rtDuration));
            time += rtDuration;

            THROW_IF_FAILED(transform->ProcessInput(0, sample.get(), 0));
        }

        GetThreadTermination.store(true);
        GetThread.join();
    }

    wil::com_ptr<IMFTransform> transform;
};

int main()
{
    winrt::init_apartment();
    THROW_IF_FAILED(MFStartup(MF_VERSION, 0));
    auto&& Scope = wil::scope_exit([] { THROW_IF_FAILED(MFShutdown()); });
    Application Application;
    Application.Run();
}

