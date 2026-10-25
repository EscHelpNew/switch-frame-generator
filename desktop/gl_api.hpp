// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/glcorearb.h>
#include <cstdio>
#include <stdexcept>
#include <string>

#define SFG_GL_FUNCTIONS(X) \
 X(PFNGLGETSTRINGPROC,glGetString) X(PFNGLGETERRORPROC,glGetError) \
 X(PFNGLVIEWPORTPROC,glViewport) X(PFNGLFINISHPROC,glFinish) \
 X(PFNGLREADPIXELSPROC,glReadPixels) X(PFNGLCLIPCONTROLPROC,glClipControl) \
 X(PFNGLGENTEXTURESPROC,glGenTextures) X(PFNGLDELETETEXTURESPROC,glDeleteTextures) \
 X(PFNGLBINDTEXTUREPROC,glBindTexture) X(PFNGLTEXPARAMETERIPROC,glTexParameteri) \
 X(PFNGLTEXSTORAGE2DPROC,glTexStorage2D) X(PFNGLCLEARTEXIMAGEPROC,glClearTexImage) X(PFNGLCOPYIMAGESUBDATAPROC,glCopyImageSubData) \
 X(PFNGLGENBUFFERSPROC,glGenBuffers) X(PFNGLDELETEBUFFERSPROC,glDeleteBuffers) \
 X(PFNGLBINDBUFFERPROC,glBindBuffer) X(PFNGLBUFFERDATAPROC,glBufferData) \
 X(PFNGLBUFFERSUBDATAPROC,glBufferSubData) X(PFNGLBINDBUFFERBASEPROC,glBindBufferBase) \
 X(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC,glDeleteFramebuffers) \
 X(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC,glFramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,glCheckFramebufferStatus) X(PFNGLBLITFRAMEBUFFERPROC,glBlitFramebuffer) \
 X(PFNGLCREATESHADERPROC,glCreateShader) X(PFNGLSHADERSOURCEPROC,glShaderSource) \
 X(PFNGLCOMPILESHADERPROC,glCompileShader) X(PFNGLGETSHADERIVPROC,glGetShaderiv) \
 X(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) X(PFNGLDELETESHADERPROC,glDeleteShader) \
 X(PFNGLCREATEPROGRAMPROC,glCreateProgram) X(PFNGLATTACHSHADERPROC,glAttachShader) \
 X(PFNGLLINKPROGRAMPROC,glLinkProgram) X(PFNGLGETPROGRAMIVPROC,glGetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC,glDeleteProgram) \
 X(PFNGLUSEPROGRAMPROC,glUseProgram) X(PFNGLDISPATCHCOMPUTEPROC,glDispatchCompute) \
 X(PFNGLMEMORYBARRIERPROC,glMemoryBarrier) X(PFNGLBINDIMAGETEXTUREPROC,glBindImageTexture) \
 X(PFNGLACTIVETEXTUREPROC,glActiveTexture) X(PFNGLGENSAMPLERSPROC,glGenSamplers) \
 X(PFNGLDELETESAMPLERSPROC,glDeleteSamplers) X(PFNGLSAMPLERPARAMETERIPROC,glSamplerParameteri) \
 X(PFNGLSAMPLERPARAMETERFVPROC,glSamplerParameterfv) X(PFNGLBINDSAMPLERPROC,glBindSampler) \
 X(PFNGLGENVERTEXARRAYSPROC,glGenVertexArrays) X(PFNGLDELETEVERTEXARRAYSPROC,glDeleteVertexArrays) \
 X(PFNGLBINDVERTEXARRAYPROC,glBindVertexArray) X(PFNGLDRAWARRAYSPROC,glDrawArrays) \
 X(PFNGLGENQUERIESPROC,glGenQueries) X(PFNGLDELETEQUERIESPROC,glDeleteQueries) \
 X(PFNGLQUERYCOUNTERPROC,glQueryCounter) X(PFNGLGETQUERYOBJECTUI64VPROC,glGetQueryObjectui64v)
#define DECLARE_GL(type,name) inline type name=nullptr;
SFG_GL_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL

inline void check_gl(const char* where) {
    GLenum error=glGetError();
    if(error!=GL_NO_ERROR) throw std::runtime_error(std::string(where)+": OpenGL error "+std::to_string(error));
}

class Window {
public:
    HWND hwnd{}; HDC dc{}; HGLRC context{};
    HWND toggle_button{},adaptive_button{},pause_button{},slow_button{};
    bool running{true}; unsigned action{};
    bool clicked{}; int click_x{},click_y{};
    unsigned display_hz{};
    int swap_interval{};
    Window() {
        try { initialize(); } catch(...) { cleanup(); throw; }
    }
    ~Window() { cleanup(); }
    void show() { ShowWindow(hwnd,SW_SHOW); SetForegroundWindow(hwnd); }
    void generator_button(bool enabled) {
        if(button_state_==int(enabled)) return;
        button_state_=int(enabled);
        SetWindowTextW(toggle_button,enabled?L"Генератор: ВКЛ":L"Генератор: ВЫКЛ");
        InvalidateRect(toggle_button,nullptr,TRUE);
    }
    void adaptive_mode_button(bool enabled) {
        if(adaptive_state_==int(enabled)) return;
        adaptive_state_=int(enabled);
        SetWindowTextW(adaptive_button,enabled?L"20–30 → 40: ВКЛ":L"20–30 → 40: ВЫКЛ");
        InvalidateRect(adaptive_button,nullptr,TRUE);
    }
    void input_buttons(bool paused,bool slow) {
        if(pause_state_!=int(paused)) {
            pause_state_=int(paused); SetWindowTextW(pause_button,paused?L"Пауза: ВКЛ":L"Пауза: ВЫКЛ");
            InvalidateRect(pause_button,nullptr,TRUE);
        }
        if(slow_state_!=int(slow)) {
            slow_state_=int(slow); SetWindowTextW(slow_button,slow?L"Замедление: ВКЛ":L"Замедление: ВЫКЛ");
            InvalidateRect(slow_button,nullptr,TRUE);
        }
    }
    void sync_target(unsigned fps) {
        const int interval=fps && display_hz%fps==0?int(display_hz/fps):0;
        if(interval==swap_interval) return;
        using Interval=BOOL(WINAPI*)(int);
        auto set=reinterpret_cast<Interval>(wglGetProcAddress("wglSwapIntervalEXT"));
        if(set && set(interval)) swap_interval=interval;
    }
    void messages() {
        MSG msg;
        while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT) running=false;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
    }
private:
    HGLRC legacy_{};
    int button_state_{-1},adaptive_state_{-1},pause_state_{-1},slow_state_{-1};
    void initialize() {
        WNDCLASSA wc{}; wc.lpfnWndProc=procedure; wc.hInstance=GetModuleHandleA(nullptr);
        wc.lpszClassName="SfgDesktopWindow"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassA(&wc);
        const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;
        RECT rect{0,0,1280,720}; AdjustWindowRect(&rect,style,FALSE);
        hwnd=CreateWindowA(wc.lpszClassName,"LSFG Desktop - preparing shaders",style,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,
            nullptr,nullptr,wc.hInstance,this);
        if(!hwnd) throw std::runtime_error("CreateWindow failed");
        toggle_button=CreateWindowW(L"BUTTON",L"Генератор: ВКЛ",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            1030,146,230,38,hwnd,reinterpret_cast<HMENU>(1001),wc.hInstance,nullptr);
        if(!toggle_button) throw std::runtime_error("Generator toggle button failed");
        SendMessageW(toggle_button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        adaptive_button=CreateWindowW(L"BUTTON",L"20–30 → 40: ВКЛ",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            1030,192,230,38,hwnd,reinterpret_cast<HMENU>(1002),wc.hInstance,nullptr);
        if(!adaptive_button) throw std::runtime_error("Adaptive mode button failed");
        SendMessageW(adaptive_button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        pause_button=CreateWindowW(L"BUTTON",L"Пауза: ВЫКЛ",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            1030,238,230,38,hwnd,reinterpret_cast<HMENU>(1003),wc.hInstance,nullptr);
        slow_button=CreateWindowW(L"BUTTON",L"Замедление: ВЫКЛ",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            1030,284,230,38,hwnd,reinterpret_cast<HMENU>(1004),wc.hInstance,nullptr);
        if(!pause_button||!slow_button) throw std::runtime_error("Input buttons failed");
        SendMessageW(pause_button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(slow_button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        dc=GetDC(hwnd);
        PIXELFORMATDESCRIPTOR p{}; p.nSize=sizeof(p); p.nVersion=1;
        p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        p.iPixelType=PFD_TYPE_RGBA; p.cColorBits=32;
        int format=ChoosePixelFormat(dc,&p);
        if(!format || !SetPixelFormat(dc,format,&p)) throw std::runtime_error("OpenGL pixel format failed");
        legacy_=wglCreateContext(dc);
        if(!legacy_ || !wglMakeCurrent(dc,legacy_)) throw std::runtime_error("OpenGL context failed");
        using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
        auto create=reinterpret_cast<Create>(wglGetProcAddress("wglCreateContextAttribsARB"));
        if(!create) throw std::runtime_error("OpenGL 4.5 core context unavailable");
        const int attributes[]{0x2091,4,0x2092,5,0x9126,1,0};
        context=create(dc,nullptr,attributes);
        if(!context) throw std::runtime_error("OpenGL 4.5 core context creation failed");
        wglMakeCurrent(nullptr,nullptr); wglDeleteContext(legacy_); legacy_=nullptr;
        if(!wglMakeCurrent(dc,context)) throw std::runtime_error("wglMakeCurrent failed");
        auto library=GetModuleHandleA("opengl32.dll");
#define LOAD_GL(type,name) { auto p=wglGetProcAddress(#name); if(!p || p==reinterpret_cast<PROC>(1) || p==reinterpret_cast<PROC>(2) || p==reinterpret_cast<PROC>(3) || p==reinterpret_cast<PROC>(-1)) p=GetProcAddress(library,#name); name=reinterpret_cast<type>(p); if(!name) throw std::runtime_error("Missing OpenGL function " #name); }
        SFG_GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
        using Interval=BOOL(WINAPI*)(int);
        auto interval=reinterpret_cast<Interval>(wglGetProcAddress("wglSwapIntervalEXT"));
        if(interval) interval(0); // Our explicit 60 Hz limiter also works on 120/144 Hz monitors.
        DEVMODEA display{}; display.dmSize=sizeof(display);
        if(EnumDisplaySettingsA(nullptr,ENUM_CURRENT_SETTINGS,&display)) display_hz=display.dmDisplayFrequency;
        glClipControl(GL_LOWER_LEFT,GL_ZERO_TO_ONE);
        std::printf("GPU=%s\nOpenGL=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
        std::fflush(stdout);
    }
    void cleanup() {
        wglMakeCurrent(nullptr,nullptr);
        if(context) wglDeleteContext(context);
        if(legacy_) wglDeleteContext(legacy_);
        if(dc && hwnd) ReleaseDC(hwnd,dc);
        if(hwnd) DestroyWindow(hwnd);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self=reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTA*>(lparam)->lpCreateParams);
            SetWindowLongPtrA(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && message==WM_CLOSE) { self->running=false; return 0; }
        if(self && message==WM_COMMAND && LOWORD(wparam)==1001 && HIWORD(wparam)==BN_CLICKED) {
            self->action='G'; SetFocus(hwnd); return 0;
        }
        if(self && message==WM_COMMAND && LOWORD(wparam)==1002 && HIWORD(wparam)==BN_CLICKED) {
            self->action='V'; SetFocus(hwnd); return 0;
        }
        if(self && message==WM_COMMAND && HIWORD(wparam)==BN_CLICKED && (LOWORD(wparam)==1003||LOWORD(wparam)==1004)) {
            self->action=LOWORD(wparam)==1003?'B':'Y'; SetFocus(hwnd); return 0;
        }
        if(self && message==WM_DRAWITEM) {
            auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if(item->CtlID>=1001 && item->CtlID<=1004) {
                const bool active=item->CtlID==1001?self->button_state_==1:item->CtlID==1002?self->adaptive_state_==1:
                    item->CtlID==1003?self->pause_state_==1:self->slow_state_==1;
                const COLORREF color=active?RGB(65,220,240):RGB(38,49,64);
                HBRUSH brush=CreateSolidBrush((item->itemState&ODS_SELECTED)?RGB(20,140,170):color);
                FillRect(item->hDC,&item->rcItem,brush); DeleteObject(brush);
                HBRUSH border=CreateSolidBrush(active?RGB(160,250,255):RGB(75,95,115));
                FrameRect(item->hDC,&item->rcItem,border); DeleteObject(border);
                wchar_t label[128]{}; GetWindowTextW(item->hwndItem,label,128);
                SetBkMode(item->hDC,TRANSPARENT);
                SetTextColor(item->hDC,active?RGB(8,30,40):RGB(155,175,195));
                auto previous=SelectObject(item->hDC,GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(item->hDC,label,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                SelectObject(item->hDC,previous);
                if(item->itemState&ODS_FOCUS) { auto rect=item->rcItem; InflateRect(&rect,-4,-4); DrawFocusRect(item->hDC,&rect); }
                return TRUE;
            }
        }
        if(self && message==WM_LBUTTONDOWN) {
            self->clicked=true;
            self->click_x=short(LOWORD(lparam)); self->click_y=short(HIWORD(lparam));
            return 0;
        }
        if(self && message==WM_KEYDOWN && !(lparam&(1L<<30))) {
            if(wparam==VK_ESCAPE) self->running=false;
            else self->action=unsigned(wparam);
            return 0;
        }
        return DefWindowProcA(hwnd,message,wparam,lparam);
    }
};
