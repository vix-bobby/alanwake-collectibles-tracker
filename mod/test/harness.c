/* Loads the proxy d3d9.dll like a game would: device, frames, Reset, more frames.
 * Saves the last frame as frame.bmp.   harness.exe <path to proxy d3d9.dll> */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define CHECK(call)                                                          \
    do {                                                                     \
        HRESULT hr_ = (call);                                                \
        if (FAILED(hr_)) {                                                   \
            printf("FAIL line %d: 0x%08lx\n", __LINE__, (unsigned long)hr_); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* What the game does with its Direct3D object: load the library, create the interface,
 * release it and call FreeLibrary. Direct3D is then used again; here through the system
 * library, which does not give the proxy another chance to be called first. */
static int unload_then_reuse(const char *path)
{
    HMODULE proxy = LoadLibraryA(path), system;
    IDirect3D9 *(WINAPI * create)(UINT);
    IDirect3D9 *d3d;
    IDirect3DDevice9 *device;
    D3DPRESENT_PARAMETERS pp = {0};
    char dll[MAX_PATH];

    if (!proxy)
        return 0;
    create = (void *)GetProcAddress(proxy, "Direct3DCreate9");
    if (!create || !(d3d = create(D3D_SDK_VERSION)))
        return 0;
    IDirect3D9_Release(d3d);
    FreeLibrary(proxy);

    GetSystemDirectoryA(dll, MAX_PATH);
    strcat(dll, "\\d3d9.dll");
    system = LoadLibraryA(dll);
    create = (void *)GetProcAddress(system, "Direct3DCreate9");
    if (!create || !(d3d = create(D3D_SDK_VERSION)))
        return 0;
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = CreateWindowA("STATIC", "probe", WS_POPUP, 0, 0, 64, 64, 0, 0, 0, 0);
    CHECK(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, pp.hDeviceWindow,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
    CHECK(IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL));
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    printf("unload and reuse ok\n");
    return 1;
}

int main(int argc, char **argv)
{
    HMODULE dll;
    IDirect3D9 *(WINAPI * create)(UINT);
    IDirect3D9 *d3d;
    IDirect3DDevice9 *device;
    IDirect3DSurface9 *back, *copy;
    D3DPRESENT_PARAMETERS pp = {0};
    D3DLOCKED_RECT locked;
    BITMAPFILEHEADER file = {0};
    BITMAPINFOHEADER info = {0};
    HWND window;
    FILE *f;
    int frame, y, round;
    DWORD blend = 0;
    const int width = 1280, height = 720;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!unload_then_reuse(argc > 1 ? argv[1] : "d3d9.dll")) {
        printf("FAIL: unload and reuse\n");
        return 1;
    }
    dll = LoadLibraryA(argc > 1 ? argv[1] : "d3d9.dll");
    if (!dll) {
        printf("FAIL: LoadLibrary error %lu\n", GetLastError());
        return 1;
    }
    create = (void *)GetProcAddress(dll, "Direct3DCreate9");
    if (!create || !(d3d = create(D3D_SDK_VERSION))) {
        printf("FAIL: Direct3DCreate9\n");
        return 1;
    }
    window = CreateWindowA("STATIC", "harness", WS_POPUP, 0, 0, width, height, 0, 0, 0, 0);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = window;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    CHECK(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, window,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
    for (round = 0; round < 3; round++) {
        for (frame = 0; frame < 30; frame++) {
            CHECK(IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0xff304860, 1, 0));
            CHECK(IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, FALSE));
            CHECK(IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL));
            IDirect3DDevice9_GetRenderState(device, D3DRS_ALPHABLENDENABLE, &blend);
            if (blend) {
                printf("FAIL: overlay changed device state\n");
                return 1;
            }
            Sleep(40); /* long enough for the profile to settle and load */
        }
        if (round < 2) {
            CHECK(IDirect3DDevice9_Reset(device, &pp));
            printf("reset %d ok\n", round + 1);
        }
    }
    CHECK(IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back));
    CHECK(IDirect3DDevice9_CreateOffscreenPlainSurface(device, width, height, D3DFMT_X8R8G8B8,
                                                       D3DPOOL_SYSTEMMEM, &copy, NULL));
    CHECK(IDirect3DDevice9_GetRenderTargetData(device, back, copy));
    CHECK(IDirect3DSurface9_LockRect(copy, &locked, NULL, D3DLOCK_READONLY));
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof file + sizeof info;
    file.bfSize = file.bfOffBits + width * height * 4;
    info.biSize = sizeof info;
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    f = fopen("frame.bmp", "wb");
    fwrite(&file, sizeof file, 1, f);
    fwrite(&info, sizeof info, 1, f);
    for (y = 0; y < height; y++)
        fwrite((char *)locked.pBits + y * locked.Pitch, 4, width, f);
    fclose(f);
    {
        int x, painted = 0;

        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                painted += (((unsigned *)((char *)locked.pBits + y * locked.Pitch))[x] & 0xffffff)
                           != 0x304860;
        if (painted < 500) {
            printf("FAIL: no overlay on the frame\n");
            return 1;
        }
        printf("overlay drawn (%d pixels)\n", painted);
    }
    IDirect3DSurface9_UnlockRect(copy);
    IDirect3DSurface9_Release(copy);
    IDirect3DSurface9_Release(back);
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    printf("PASS\n");
    return 0;
}
