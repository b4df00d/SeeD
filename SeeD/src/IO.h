#pragma once

#include <WinUser.h>
#include "Shellapi.h"

#include <io.h>
#include <fcntl.h>

#define VK_0 0x30
#define VK_1 0x31
#define VK_2 0x32
#define VK_3 0x33
#define VK_4 0x34
#define VK_5 0x35
#define VK_6 0x36
#define VK_7 0x37
#define VK_8 0x38
#define VK_9 0x39

#define VK_A 0x41
#define VK_B 0x42
#define VK_C 0x43
#define VK_D 0x44
#define VK_E 0x45
#define VK_F 0x46
#define VK_G 0x47
#define VK_H 0x48
#define VK_I 0x49
#define VK_J 0x4A
#define VK_K 0x4B
#define VK_L 0x4C
#define VK_M 0x4D
#define VK_N 0x4E
#define VK_O 0x4F
#define VK_P 0x50
#define VK_Q 0x51
#define VK_R 0x52
#define VK_S 0x53
#define VK_T 0x54
#define VK_U 0x55
#define VK_V 0x56
#define VK_W 0x57
#define VK_X 0x58
#define VK_Y 0x59
#define VK_Z 0x5A


LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

class IOs
{
public:

	static IOs* instance;

    struct WindowInformation
    {
		HINSTANCE windowInstance{};
		HWND windowHandle{};
		LPCWSTR windowName{};

        bool fullScreen;
        bool usevSync;
		bool windowResized;

        char dropFile[256] = "";

        uint2 windowResolution;
        int2 windowPos;
    };

    struct Keys
    {
        bool pressed[256]; // true the frame that the key is pressed down
        bool down[256]; // true as long as the key is pressed down
    };
    struct Mouse
    {
        int3 mousePos;
        int3 mouseDelta;

        bool mouseButtonLeft;
        bool mouseButtonMiddle;
        bool mouseButtonRight;
        bool mouseButtonLeftDown;
        bool mouseButtonMiddleDown;
        bool mouseButtonRightDown;
        bool mouseButtonLeftUp;
        bool mouseButtonMiddleUp;
        bool mouseButtonRightUp;
        float mouseWheel;

        bool mouseDrag;
        int3 mouseStartDrag;
        int3 mouseStopDrag;
    };

	Keys _keys{};
	Keys keys{};
	Mouse _mouse{};
	Mouse mouse{};
    WindowInformation window;

    void GetMouse()
    {
        ZoneScoped;
    }

    void GetWindow()
    {
        ZoneScoped;
    }

    bool Resize()
    {
		bool ret = window.windowResized;
		window.windowResized = false;
		return ret;
    }

    bool DropFile()
    {
        return strcmp(window.dropFile, "") != 0;
    }

    void Update()
    {
        ZoneScoped;
        int3 mousePosLastFrame = _mouse.mousePos;
        POINT p;
        GetCursorPos(&p);
        ::ScreenToClient(window.windowHandle, &p);
        _mouse.mousePos.x = p.x;
        _mouse.mousePos.y = p.y;
        _mouse.mouseDelta = _mouse.mousePos - mousePosLastFrame;

        if (_mouse.mouseButtonLeftDown)
            _mouse.mouseStartDrag = _mouse.mousePos;
        if (_mouse.mouseButtonLeft)
            if ((float)_mouse.mouseStartDrag.x != _mouse.mousePos.x || (float)_mouse.mouseStartDrag.y != _mouse.mousePos.y)
                _mouse.mouseDrag = true;
        if (_mouse.mouseButtonLeftUp)
            _mouse.mouseStopDrag = _mouse.mousePos;
        if (!_mouse.mouseButtonLeft && !_mouse.mouseButtonLeftUp)
            _mouse.mouseDrag = false;

        memcpy(&mouse, &_mouse, sizeof(Mouse));
        memcpy(&keys, &_keys, sizeof(Keys));

        for (unsigned int i = 0; i < 256; i++)
        {
            _keys.pressed[i] = false;
        }
        _mouse.mouseButtonLeftDown = false;
        _mouse.mouseButtonMiddleDown = false;
        _mouse.mouseButtonRightDown = false;
        _mouse.mouseButtonLeftUp = false;
        _mouse.mouseButtonMiddleUp = false;
        _mouse.mouseButtonRightUp = false;
        _mouse.mouseWheel = 0;
        strcpy(window.dropFile, "");
    }

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam)
	{
		ZoneScoped;

		if (ImGui_ImplWin32_WndProcHandler(hwnd, umessage, wparam, lparam))
			return 1;

		bool WantCaptureKeyboard = false;
		bool WantCaptureMouse = false;
		if (ImGui::GetCurrentContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			WantCaptureKeyboard = !io.WantCaptureKeyboard;
			WantCaptureMouse = !io.WantCaptureMouse;
		}

		IOs* pThis;
		if (umessage == WM_NCCREATE)
		{
			pThis = static_cast<IOs*>(reinterpret_cast<CREATESTRUCT*>(lparam)->lpCreateParams);

			SetLastError(0);
			if (!SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
			{
				if (GetLastError() != 0)
					return FALSE;
			}
		}
		else
		{
			pThis = reinterpret_cast<IOs*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		}
		WindowInformation& window = pThis->window;
		Keys& _keys = pThis->_keys;
		Mouse& _mouse = pThis->_mouse;

		WINDOWPLACEMENT plcmt;
		GetWindowPlacement(hwnd, &plcmt);
		window.windowPos.x = (int)plcmt.rcNormalPosition.left;
		window.windowPos.y = (int)plcmt.rcNormalPosition.top;

		switch (umessage)
		{
		case WM_CREATE:
		{
			DragAcceptFiles(hwnd, TRUE);
			break;
		}
		case WM_DROPFILES:
		{
			HDROP drop = (HDROP)wparam;      // = (WPARAM) (HDROP) hDrop;
			POINT pt;
			WORD cFiles, a;
			WCHAR lpszFile[256];
			DragQueryPoint(drop, &pt);
			cFiles = DragQueryFile(drop, 0xFFFFFFFF, (LPWSTR)NULL, 0);
			for (a = 0; a < cFiles; pt.y += 20, a++) {
				DragQueryFile(drop, a, lpszFile, sizeof(lpszFile));
				//MessageBox(hwnd, lpszFile, L"Title", MB_OK | MB_ICONINFORMATION);
				String tmp(WCharToString(lpszFile));
#pragma warning(push)
#pragma warning(disable: 4244)
				strcpy(window.dropFile, std::string(tmp.begin(), tmp.end()).c_str());
#pragma warning(pop)
			}
			DragFinish(drop);
			break;
		}

		case WM_DESTROY:
		{
			DragAcceptFiles(hwnd, FALSE);
			PostQuitMessage(0);
			break;
		}
		case WM_CLOSE:
		{
			PostQuitMessage(0);
			break;
		}

		case WM_SIZE:
		{
			if (window.windowHandle != nullptr)
			{
				RECT r;
				::GetClientRect(window.windowHandle, &r);
				if (r.right > 0 && r.right < 4096 && r.left > 0 && r.left < 4096)
				{
					if (window.windowResolution.x != r.right || window.windowResolution.y != r.bottom)
					{
						window.windowResolution.x = r.right;
						window.windowResolution.y = r.bottom;
						window.windowResized = true;
					}
				}
			}
			break;
		}

		// Check if a key has been pressed on the keyboard.
		// unlike WM_LBUTTONDOWN, WM_KEYDOWN is called as long as a key is pressed
		case WM_KEYDOWN:
		{
			if ((unsigned int)wparam == VK_ESCAPE)
			{
				ImGui::SetWindowFocus(NULL);
			}
			if (WantCaptureKeyboard)
			{
				// If a key is pressed send it to the input object so it can record that state.
				_keys.pressed[((unsigned int)wparam)] = !_keys.pressed[((unsigned int)wparam)];
				_keys.down[((unsigned int)wparam)] = true;
			}
			break;
		}

		// Check if a key has been released on the keyboard.
		case WM_KEYUP:
		{
			if (WantCaptureKeyboard)
			{
				// If a key is released then send it to the input object so it can unset the state for that key.
				_keys.down[((unsigned int)wparam)] = false;
			}
			break;
		}

		// unlike WM_KEYDOWN mouse WM_LBUTTONDOWN only happen once
		case WM_LBUTTONDOWN:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonLeft = true;
				_mouse.mouseButtonLeftDown = true;
			}
			break;
		}

		case WM_MBUTTONDOWN:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonMiddle = true;
				_mouse.mouseButtonMiddleDown = true;
			}
			break;
		}

		case WM_RBUTTONDOWN:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonRight = true;
				_mouse.mouseButtonRightDown = true;
			}
			break;
		}

		case WM_LBUTTONUP:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonLeft = false;
				_mouse.mouseButtonLeftUp = true;
			}
			break;
		}

		case WM_MBUTTONUP:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonMiddle = false;
				_mouse.mouseButtonMiddleUp = true;
			}
			break;
		}

		case WM_RBUTTONUP:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseButtonRight = false;
				_mouse.mouseButtonRightUp = true;
			}
			break;
		}

		case WM_MOUSEWHEEL:
		{
			if (WantCaptureMouse)
			{
				_mouse.mouseWheel = GET_WHEEL_DELTA_WPARAM(wparam);
			}
			break;
		}

		// Any other messages send to the default message handler as our application won't make use of them.
		default:
		{
			return DefWindowProc(hwnd, umessage, wparam, lparam);
		}
		}

		return 0;
	}

	void On(WindowInformation _window)
	{
		ZoneScoped;

		window = _window;

		WNDCLASSEX wc;
		//DEVMODE dmScreenSettings;
		int2 windowPosition;
		uint2 screenSize;

		window.windowName = L"SeeD";

		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = (WNDPROC)WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = window.windowInstance;
		wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
		wc.hIconSm = wc.hIcon;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = L"SeeD";
		wc.cbSize = sizeof(WNDCLASSEX);

		RegisterClassEx(&wc);

		screenSize.x = GetSystemMetrics(SM_CXSCREEN);
		screenSize.y = GetSystemMetrics(SM_CYSCREEN);
		windowPosition = 0;

		if (window.fullScreen)
		{
			window.windowResolution = uint2(screenSize.x, screenSize.y);
			if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
				std::cout << "SetProcessDpiAwarenessContext failed : ";
			/*
			DISPLAY_DEVICE dev;
			memset(&dev, 0, sizeof(dev));
			dev.cb = sizeof(dev);
			EnumDisplayDevices(NULL, 0, &dev, 0);

			DEVMODE mode;
			memset(&mode, 0, sizeof(mode));
			mode.dmSize = sizeof(mode);
			mode.dmBitsPerPel = 32;
			mode.dmPelsWidth = screenSize.x;
			mode.dmPelsHeight = screenSize.y;
			mode.dmDisplayFrequency = 60;
			mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

			LONG status = ChangeDisplaySettingsEx(dev.DeviceName, &mode, NULL, CDS_FULLSCREEN, NULL);
			if (status != DISP_CHANGE_SUCCESSFUL)
			{
				const char* reason = "Unknown reason";
				switch (status)
				{
				case DISP_CHANGE_BADFLAGS:
					reason = "DISP_CHANGE_BADFLAGS";
					break;
				case DISP_CHANGE_BADMODE:
					reason = "DISP_CHANGE_BADMODE";
					break;
				case DISP_CHANGE_BADPARAM:
					reason = "DISP_CHANGE_BADPARAM";
					break;
				case DISP_CHANGE_FAILED:
					reason = "DISP_CHANGE_FAILED";
					break;
				}
				std::cout << "ChangeDisplaySettingsEx() failed: " << reason;
			}
			*/

			window.windowHandle = CreateWindowEx(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
				window.windowName, window.windowName,
				WS_POPUP
				//| WS_SIZEBOX
				//WS_POPUPWINDOW | WS_CAPTION
				//| WS_VISIBLE
				,
				(int)windowPosition.x, (int)windowPosition.y, (int)window.windowResolution.x, (int)window.windowResolution.y,
				NULL, NULL, window.windowInstance, this);
		}
		else
		{
			
			int px = (int)((float)screenSize.x - (float)window.windowResolution.x) * 0.5f;
			int py = (int)((float)screenSize.y - (float)window.windowResolution.y) * 0.5f;
			windowPosition = int2(px, py);
			int sx = (int)window.windowResolution.x;
			int sy = (int)window.windowResolution.y;

			window.windowHandle = CreateWindowEx(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
				window.windowName, window.windowName,
				//WS_POPUP
				//| WS_SIZEBOX
				WS_POPUPWINDOW | WS_CAPTION
				//| WS_VISIBLE
				,
				px, py, sx, sy,
				NULL, NULL, window.windowInstance, this);

			/*
			//::GetClientRect(window.windowHandle, &r); is fucked up in release, why ?!
			*/

			RECT r{};
			::GetClientRect(window.windowHandle, &r);
			uint2 windowResolution = window.windowResolution;
			windowResolution += uint2((int)window.windowResolution.x - r.right, (int)window.windowResolution.y - r.bottom);

			if (windowResolution.x <= 0 || windowResolution.y <= 0)
				IOs::Log("bad resolution {} {}", (int)windowResolution.x, (int)windowResolution.y);

			DestroyWindow(window.windowHandle);

			window.windowHandle = CreateWindowEx(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
				window.windowName, window.windowName,
				//WS_POPUP
				//| WS_SIZEBOX
				WS_POPUPWINDOW | WS_CAPTION
				//| WS_VISIBLE
				,
				px, py, (int)windowResolution.x, (int)windowResolution.y,
				NULL, NULL, window.windowInstance, this);
		}


		RedirectIOToConsole();

		ShowWindow(window.windowHandle, window.fullScreen ? SW_MAXIMIZE : SW_SHOW);
		SetForegroundWindow(window.windowHandle);
		SetFocus(window.windowHandle);
		UpdateWindow(window.windowHandle);

		instance = this;
		return;
	}

	void Off()
	{
		instance = nullptr;
	}

	void ProcessMessages()
	{
		MSG msg;
		// Handle the windows messages.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		/*
		if (msg.message == WM_QUIT)
		{
			Running = false;
		}
		*/
	}

	void RedirectIOToConsole()
	{
		static const WORD MAX_CONSOLE_LINES = 500;
		int hConHandle;
		HANDLE lStdHandle;
		CONSOLE_SCREEN_BUFFER_INFO coninfo;
		FILE* fp;

		// allocate a console for this app
		AllocConsole();

		// set the screen buffer to be big enough to let us scroll text
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
		coninfo.dwSize.Y = MAX_CONSOLE_LINES;
		SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

		// redirect unbuffered STDOUT to the console
		lStdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);

		fp = _fdopen(hConHandle, "w");

		*stdout = *fp;

		setvbuf(stdout, NULL, _IONBF, 0);

		freopen_s(&fp, "CONOUT$", "w", stdout);

		// redirect unbuffered STDIN to the console

		lStdHandle = GetStdHandle(STD_INPUT_HANDLE);
		hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);

		fp = _fdopen(hConHandle, "r");
		*stdin = *fp;
		setvbuf(stdin, NULL, _IONBF, 0);

		// redirect unbuffered STDERR to the console
		lStdHandle = GetStdHandle(STD_ERROR_HANDLE);
		hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);

		fp = _fdopen(hConHandle, "w");

		*stderr = *fp;

		setvbuf(stderr, NULL, _IONBF, 0);

		// make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog
		// point to console as well
		std::ios::sync_with_stdio();
	}

	template<typename... Args>
	static void Log(std::string_view rt_fmt_str, Args&&... args)
	{
		std::string message = std::vformat(rt_fmt_str, std::make_format_args(args...));
		message.append("\n");
		std::cout << message;
		OutputDebugStringA(message.c_str());
	}
	template<typename... Args>
	static void Log(std::wstring_view rt_fmt_str, Args&&... args)
	{
		String message = std::vformat(rt_fmt_str, std::make_wformat_args(args...));
		message.append("\n");
		std::wcout << message;
		OutputDebugStringW(message.c_str());
	}
};
IOs* IOs::instance;