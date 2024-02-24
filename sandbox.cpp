#include <iostream>
#include<d3d11.h>
#include<dxgi1_2.h>
#include<wincodec.h>
#include<d2d1.h>
#include<mftransform.h>
#include<mfobjects.h>
#include<mfapi.h>
#include<Mferror.h>
#include<Mferror.h>
#include<wmcodecdsp.h>
#include<codecapi.h>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mf.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"wmcodecdspuuid.lib")

#pragma comment(lib,"dxguid.lib")

DWORD WINAPI Get(LPVOID lpParam);

IMFTransform* transform = NULL;

int main()
{
    ID3D11Device* device = NULL;
    IDXGIDevice1* dxgiDevice = NULL;
    IDXGIAdapter* dxgiAdapter = NULL;
    IDXGIOutput* output = NULL;
    IDXGIOutput1* output1 = NULL;
    IDXGIOutputDuplication* dxgiOutput = NULL;

    IDXGISurface* dxgiSurface = NULL;

    ID3D11DeviceContext* context = NULL;

    IMFMediaType* mediaType = NULL;
    IMFMediaType* outputMediaType = NULL;
    IUnknown* transformUnk = NULL;


    UINT resetToken;
    IMFDXGIDeviceManager* deviceManager = NULL;
    MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &device, &featureLevel, &context);
    hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);

    hr = dxgiAdapter->EnumOutputs(0, &output);
    hr = output->QueryInterface(IID_PPV_ARGS(&output1));
    output->Release();
    hr = output1->DuplicateOutput(device, &dxgiOutput);
    output1->Release();
    IDXGIResource* resource = NULL;

    ID3D11Texture2D* texture = NULL;


    hr = MFStartup(MF_VERSION, 0);
    MFCreateMediaType(&mediaType);
    MFCreateMediaType(&outputMediaType);

    outputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeRatio(outputMediaType, MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeSize(outputMediaType, MF_MT_FRAME_SIZE, 1920, 1080);
    outputMediaType->SetUINT32(MF_MT_AVG_BITRATE, 1920 * 1080 * 30);
    outputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    outputMediaType->SetUINT32(CODECAPI_AVLowLatencyMode, true);
    hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&transformUnk);
    hr = transformUnk->QueryInterface(IID_PPV_ARGS(&transform));
    transform->SetOutputType(0, outputMediaType, 0);
    transform->GetInputAvailableType(0, 0, &mediaType);
    mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    transform->SetInputType(0, mediaType, 0);
    transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager));

    hr = deviceManager->ResetDevice(device, resetToken);

    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
    LPDWORD lpThreadId = 0;
    CreateThread(NULL, 0, Get, NULL, 0, lpThreadId);

    LONGLONG time = 0;

    UINT64 rtDuration = 0;

    hr = MFFrameRateToAverageTimePerFrame(30, 1, &rtDuration);
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc;
    ZeroMemory(&contentDesc, sizeof(contentDesc));
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = 1920;
    contentDesc.InputHeight = 1080;
    contentDesc.OutputWidth = 1920;
    contentDesc.OutputHeight = 1080;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    ID3D11VideoProcessorEnumerator* vpe;
    ID3D11VideoDevice* videoDevice;
    hr = device->QueryInterface(IID_PPV_ARGS(&videoDevice));
    hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &vpe);
    D3D11_VIDEO_PROCESSOR_CAPS caps;
    ID3D11VideoProcessor* processor = NULL;
    hr = vpe->GetVideoProcessorCaps(&caps);
    bool stereo = caps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO;
    DXGI_FORMAT format1 = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT format = DXGI_FORMAT_NV12;
    RECT src = { 0,0,1920,1080 };
    RECT dest = { 0,0,1920,1080 };

    UINT uiFlags;
    hr = vpe->CheckVideoProcessorFormat(format, &uiFlags);
    hr = vpe->CheckVideoProcessorFormat(format1, &uiFlags);
    for (int i = 0; i < caps.RateConversionCapsCount; i++) {
        hr = videoDevice->CreateVideoProcessor(vpe, i, &processor);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    DXGI_MODE_DESC1 modelFilter;
    ZeroMemory(&modelFilter, sizeof(modelFilter));
    modelFilter.Width = 1920;
    modelFilter.Height = 1080;
    ID3D11VideoContext* videoContext;
    hr = context->QueryInterface(IID_PPV_ARGS(&videoContext));
    videoContext->VideoProcessorSetStreamFrameFormat(processor, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext->VideoProcessorSetStreamOutputRate(processor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, true, NULL);
    RECT r1;
    r1.left = r1.top = 0;
    r1.right = 1920;
    r1.bottom = 1080;
    videoContext->VideoProcessorSetStreamSourceRect(processor, 0, TRUE, &r1);
    videoContext->VideoProcessorSetStreamDestRect(processor, 0, TRUE, &r1);
    videoContext->VideoProcessorSetOutputTargetRect(processor, TRUE, &r1);
    D3D11_VIDEO_COLOR backgroundColor;
    ZeroMemory(&backgroundColor, sizeof(backgroundColor));
    backgroundColor.RGBA.A = 1.0f;
    backgroundColor.RGBA.R = 1.0f;
    backgroundColor.RGBA.G = 1.0f;
    backgroundColor.RGBA.B = 1.0f;
    videoContext->VideoProcessorSetOutputBackgroundColor(processor, FALSE, &backgroundColor);
    videoContext->VideoProcessorSetStreamOutputRate(processor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, NULL);
    ID3D11VideoProcessorInputView* inputView;
    ID3D11VideoProcessorOutputView* outputView;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc;
    ZeroMemory(&inputViewDesc, sizeof(inputViewDesc));
    inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDesc.Texture2D.ArraySlice = 0;
    inputViewDesc.Texture2D.MipSlice = 0;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc;
    ZeroMemory(&outputViewDesc, sizeof(outputViewDesc));
    outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputViewDesc.Texture2D.MipSlice = 0;
    D3D11_VIDEO_PROCESSOR_STREAM st;
    ZeroMemory(&st, sizeof(st));
    st.Enable = true;
    st.OutputIndex = 0;
    st.InputFrameOrField = 0;
    st.PastFrames = 0;
    st.FutureFrames = 0;
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    ID3D11Texture2D* texture11;
    while (true) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        hr = dxgiOutput->AcquireNextFrame(300, &frameInfo, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }

        hr = resource->QueryInterface(IID_PPV_ARGS(&texture));
        hr = videoDevice->CreateVideoProcessorInputView(texture, vpe, &inputViewDesc, &inputView);



        texture->GetDesc(&desc);
        desc.CPUAccessFlags = 0;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.MiscFlags = 0;
        desc.Format = DXGI_FORMAT_NV12;
        ID3D11Texture2D* texture1 = NULL;
        hr = device->CreateTexture2D(&desc, NULL, &texture1);
        hr = videoDevice->CreateVideoProcessorOutputView(texture1, vpe, &outputViewDesc, &outputView);
        st.pInputSurface = inputView;
        hr = videoContext->VideoProcessorBlt(processor, outputView, 0, 1, &st);

        texture1->GetDesc(&desc);
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        hr = texture1->QueryInterface(IID_PPV_ARGS(&dxgiSurface));



        inputView->Release();
        outputView->Release();
        IMFMediaBuffer* buffer;

        hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, dxgiSurface, 0, false, &buffer);

        IMFSample* sample;
        hr = MFCreateSample(&sample);
        buffer->SetCurrentLength(8294400);
        hr = sample->AddBuffer(buffer);

        sample->SetSampleTime(time);
        sample->SetSampleDuration(rtDuration);

        time += rtDuration;

        hr = transform->ProcessInput(0, sample, 0);
        if (FAILED(hr)) {
            Sleep(30);
        }
        sample->Release();
        resource->Release();
        texture->Release();
        dxgiSurface->Release();
        buffer->Release();
        //texture11->Release();
        texture1->Release();

        dxgiOutput->ReleaseFrame();
    }
}

UINT32 sampleSize = 0;
DWORD WINAPI Get(LPVOID lpParam) {
    HRESULT hr = S_OK;
    while (true) {
        IMFSample* sample;
        hr = MFCreateSample(&sample);
        if (sampleSize == 0) {

            MFT_OUTPUT_STREAM_INFO info;

            transform->GetOutputStreamInfo(0, &info);
            sampleSize = info.cbSize;
        }
        if (sampleSize) {
            IMFMediaBuffer* outputMediaBuffer;
            hr = MFCreateMemoryBuffer(static_cast<DWORD>(sampleSize), &outputMediaBuffer);
            if (SUCCEEDED(hr)) {
                sample->AddBuffer(outputMediaBuffer);
            }
            else {
                continue;
            }
            outputMediaBuffer->Release();
        }
        if (transform == NULL) {
            Sleep(30);
            continue;
        }
        MFT_OUTPUT_DATA_BUFFER outputDataBuffer = { 0,sample };
        DWORD status;
        HRESULT processOutputResult = transform->ProcessOutput(0, 1, &outputDataBuffer, &status);
        sample->Release();

        if (processOutputResult == MF_E_TRANSFORM_STREAM_CHANGE) {
            IMFMediaType* mediaType = NULL;
            transform->GetOutputAvailableType(0, 0, &mediaType);
            mediaType->GetUINT32(MF_MT_FRAME_SIZE, &sampleSize);

            transform->SetOutputType(0, mediaType, 0);

            GUID g;
            mediaType->GetGUID(MF_MT_SUBTYPE, &g);

            continue;
        }
        if (processOutputResult == MF_E_TRANSFORM_TYPE_NOT_SET) {
            IMFMediaType* mediaType = NULL;
            hr = transform->GetOutputAvailableType(0, 0, &mediaType);
            mediaType->GetUINT32(MF_MT_FRAME_SIZE, &sampleSize);
            hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, 1280, 720);

            hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, 25, 1);
            GUID g;
            mediaType->GetGUID(MF_MT_SUBTYPE, &g);
            hr = transform->SetOutputType(0, mediaType, 0);
        }
        if (processOutputResult == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            Sleep(30);
            continue;

            break;
        }
        if (FAILED(processOutputResult)) {
            IMFMediaType* mediaType = NULL;
            hr = transform->GetOutputAvailableType(0, 0, &mediaType);

            GUID g;
            mediaType->GetGUID(MF_MT_SUBTYPE, &g);
            mediaType->GetUINT32(MF_MT_SAMPLE_SIZE, &sampleSize);
            Sleep(30);
            continue;
        }
        IMFMediaBuffer* pBuffer = NULL;
        hr = sample->ConvertToContiguousBuffer(&pBuffer);

        if (FAILED(hr)) {
            continue;
        }
        BYTE* buffer = NULL;
        DWORD imageSize = 0;
        pBuffer->Lock(&buffer, NULL, &imageSize);

        pBuffer->Unlock();
        pBuffer->Release();
    }
}
