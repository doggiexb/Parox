#include "pch.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

struct KeyMapping {
    UINT x;
    UINT y;
    BYTE targetR;
    BYTE targetG;
    BYTE targetB;
    UINT scanCode;
    bool isPressed;
};

KeyMapping keyMappings[] = {
    { 730, 875, 255, 255, 255, MapVirtualKey(0x41, 0), false},
    { 885, 875, 255, 255, 255, MapVirtualKey(0x53, 0), false},
    { 1033, 875, 255, 255, 255, MapVirtualKey(VK_OEM_1, 0), false},
    { 1185, 875, 255, 255, 255, MapVirtualKey(VK_OEM_7, 0), false}
};

bool TaskStatus = false;

auto lastFrameTime = null;

extern "C" __declspec(dllexport) void __cdecl StartRead() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to initialize COM library.", "Error", MB_OK);
        return;
    }

    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &featureLevel, 1, D3D11_SDK_VERSION,
        &d3dDevice, nullptr, &d3dContext);

    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to create Direct3D Device.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to get DXGI device.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to get DXGI adapter.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to enumerate outputs.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to cast to IDXGIOutput1.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "Failed to duplicate output.", "Error", MB_OK);
        CoUninitialize();
        return;
    }

    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (TaskStatus) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        hr = deskDupl->AcquireNextFrame(0, &frameInfo, &desktopResource);

        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            else {
                MessageBoxA(NULL, "Failed to acquire next frame.", "Error", MB_OK);
                break;
            }
        }

        ComPtr<ID3D11Texture2D> desktopImage;
        hr = desktopResource.As(&desktopImage);
        if (FAILED(hr)) {
            MessageBoxA(NULL, "Failed to cast to ID3D11Texture2D.", "Error", MB_OK);
            deskDupl->ReleaseFrame();
            continue;
        }

        ComPtr<ID3D11Texture2D> stagingTexture;
        D3D11_TEXTURE2D_DESC textureDesc;
        desktopImage->GetDesc(&textureDesc);

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = textureDesc.Width;
        stagingDesc.Height = textureDesc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = textureDesc.Format;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;

        hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            MessageBoxA(NULL, "Failed to create staging texture.", "Error", MB_OK);
            deskDupl->ReleaseFrame();
            continue;
        }

        d3dContext->CopyResource(stagingTexture.Get(), desktopImage.Get());

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);

        if (TaskStatus) {
            if (SUCCEEDED(hr)) {
                UINT width = stagingDesc.Width;
                UINT height = stagingDesc.Height;
                BYTE* pData = static_cast<BYTE*>(mappedResource.pData);
                UINT rowPitch = mappedResource.RowPitch;

                for (auto& state : keyMappings) {
                    BYTE* pixel = pData + state.y * rowPitch + state.x * 4;
                    BYTE b = pixel[0];
                    BYTE g = pixel[1];
                    BYTE r = pixel[2];

                    bool colorMatches = (r == state.targetR && g == state.targetG && b == state.targetB);

                    if (colorMatches && !state.isPressed) {
                        INPUT input = {};
                        input.type = INPUT_KEYBOARD;
                        input.ki.dwFlags = KEYEVENTF_SCANCODE;
                        input.ki.wScan = state.scanCode;
                        SendInput(1, &input, sizeof(INPUT));
                        state.isPressed = true;
                    }
                    else if (!colorMatches && state.isPressed) {
                        INPUT input = {};
                        input.type = INPUT_KEYBOARD;
                        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                        input.ki.wScan = state.scanCode;
                        SendInput(1, &input, sizeof(INPUT));
                        state.isPressed = false;
                    }
                }

                d3dContext->Unmap(stagingTexture.Get(), 0);
            }
        }

        deskDupl->ReleaseFrame();

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = now - lastFrameTime;
        lastFrameTime = now;

        // Calculate sleep time to maintain frame rate
        double targetFrameDuration = 16.68; // Approximately 60 FPS
        double sleepTime = targetFrameDuration - elapsed.count();
        if (sleepTime > 0) {
            Sleep(static_cast<DWORD>(sleepTime));
        }
    }

    CoUninitialize();
}

extern "C" __declspec(dllexport) void __cdecl SwitchDllTaskStatus() {
    TaskStatus = !TaskStatus;
}

extern "C" __declspec(dllexport) void __cdecl GetFrameTime() {
    return lastFrameTime;
}
