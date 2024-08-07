#include "pch.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

std::ofstream logFile("debug.log", std::ios_base::app);

#define DEBUG(msg) logFile << msg << std::endl;

// Structures
struct KeyMapping {
    UINT x;
    UINT y;
    BYTE targetR;
    BYTE targetG;
    BYTE targetB;
    UINT scanCode;
    bool isPressed;
};

// Variables
KeyMapping keyMappings[] = {
    { 730, 875, 255, 255, 255, MapVirtualKey(0x41, 0), false },
    { 885, 875, 255, 255, 255, MapVirtualKey(0x53, 0), false },
    { 1033, 875, 255, 255, 255, MapVirtualKey(VK_OEM_1, 0), false },
    { 1185, 875, 255, 255, 255, MapVirtualKey(VK_OEM_7, 0), false }
};

HWND hwnd = nullptr;

// Atomic variables
std::atomic<bool> TaskStatus(false);

// Functions : IN

// Functions : IN End

// Functions : OUT
extern "C" __declspec(dllexport) void __cdecl startRead() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library." << std::endl;
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
        CoUninitialize();
        DEBUG("Failed to create D3D11 Device.");
        return;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        CoUninitialize();
        DEBUG("DxgiDevice failed to cast.");
        return;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        CoUninitialize();
        DEBUG("DxgiAdapter failed to cast.");
        return;
    }

    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) {
        CoUninitialize();
        DEBUG("DxgiOutput1 failed to cast.");
        return;
    }

    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) {
        CoUninitialize();
        DEBUG("DxgiOutput1 failed to cast.");
        return;
    }

    hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
    if (FAILED(hr)) {
        CoUninitialize();
        DEBUG("Failed to Duplicate Output.");
        return;
    }

    while (true) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        hr = deskDupl->AcquireNextFrame(0, &frameInfo, &desktopResource);

        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            else {
                break;
            }
        }

        ComPtr<ID3D11Texture2D> desktopImage;
        hr = desktopResource.As(&desktopImage);
        if (FAILED(hr)) {
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
            deskDupl->ReleaseFrame();
            continue;
        }

        d3dContext->CopyResource(stagingTexture.Get(), desktopImage.Get());

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);

        if (TaskStatus.load()) {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }

    CoUninitialize();
}

extern "C" __declspec(dllexport) void __cdecl switchDllTaskStatus() {
    TaskStatus.store(!TaskStatus.load());
}

// Functions :: OUT End
