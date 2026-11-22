#pragma once

#include <WinUser.h>
#include "Shellapi.h"

#include <io.h>
#include <fcntl.h>

// Game controllers: XInput for Xbox pads, winmm's joystick API for everything else Windows sees as a
// game controller (an RC transmitter in USB joystick mode or on a sim dongle, a wheel, a DirectInput pad).
#include <Xinput.h>
#include <mmsystem.h>
#pragma comment(lib, "Xinput.lib")
#pragma comment(lib, "winmm.lib")

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

    // ---- game controller ---------------------------------------------------------------------
    // One controller is read at a time (ControllerSettings::device). Its raw axes/buttons land in
    // `controller`, and the driving mapping (steering, throttle, reset) is applied here too, so a
    // consumer only reads controller.steering / controller.throttle.

    static constexpr int controllerAuto = -1;           // first XInput pad, else first joystick
    static constexpr int controllerXInput = 0;          // + XInput slot 0..3
    static constexpr int controllerJoystick = 100;      // + winmm joystick id 0..15

    struct ControllerAxisMap
    {
        int axis = -1;                  // index into Controller::axes, -1 = unused
        bool invert = false;
        float min = -1.0f;              // calibration, in raw axis units: the two ends of the travel
        float center = 0.0f;            // and where it rests (a pistol-grip trigger's neutral)
        float max = 1.0f;
        float deadzone = 0.05f;         // fraction of travel around the center that reads 0
        float expo = 0.0f;              // 0 linear .. 1 cubic, a radio's expo

        // -1..1 after calibration, deadzone, expo and invert. Each side of the center is scaled
        // separately, so an off-centre neutral still reaches +-1 at both ends.
        float Apply(const float* axes, uint axisCount) const
        {
            if (axis < 0 || axis >= (int)axisCount)
                return 0.0f;
            float v = axes[axis];
            float span = v >= center ? max - center : center - min;
            float x = span > 1.0e-4f ? (v - center) / span : 0.0f;
            x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
            float dz = deadzone < 0.0f ? 0.0f : (deadzone > 0.95f ? 0.95f : deadzone);
            float a = fabsf(x);
            a = a <= dz ? 0.0f : (a - dz) / (1.0f - dz);
            a = (1.0f - expo) * a + expo * a * a * a;
            x = x < 0.0f ? -a : a;
            return invert ? -x : x;
        }
        void ResetCalibration() { min = -1.0f; center = 0.0f; max = 1.0f; }
    };

    struct ControllerMapping
    {
        ControllerAxisMap steering;     // + = right
        ControllerAxisMap throttle;     // + = forward, - = brake, then reverse once stopped
        int resetButton = -1;           // puts the car back on its wheels, like R. -1 = none
    };

    struct ControllerSettings
    {
        bool enabled = true;
        int device = controllerAuto;
        ControllerMapping xinput;       // used when the device read is an XInput pad
        ControllerMapping joystick;     // used for any winmm joystick

        ControllerSettings()
        {
            // Xbox: left stick steers, RT - LT (axis 6) is throttle/brake, Y resets.
            xinput.steering.axis = 0;
            xinput.steering.deadzone = 0.12f;   // worn sticks rest well off 0
            xinput.steering.expo = 0.3f;
            xinput.throttle.axis = 6;
            xinput.throttle.deadzone = 0.03f;
            xinput.resetButton = 3;
            // Generic joystick / RC radio: X steers, Y (up = forward, winmm's Y grows downward) is
            // throttle. A radio's channel order varies -- pick the axes in the Controller window.
            joystick.steering.axis = 0;
            joystick.steering.deadzone = 0.02f;
            joystick.throttle.axis = 1;
            joystick.throttle.invert = true;
            joystick.throttle.deadzone = 0.03f;
        }
    };

    struct ControllerDevice
    {
        int device;
        char name[128];
        // winmm only: raw range per axis X Y Z R U V, from joyGetDevCaps
        bool hasAxis[6];
        uint axisMin[6];
        uint axisMax[6];
        uint buttonCount;
    };

    struct Controller
    {
        static constexpr uint maxAxes = 8;
        static constexpr uint maxButtons = 32;

        bool connected;
        int device;                     // what is being read, controllerXInput + n / controllerJoystick + n
        char name[128];
        uint axisCount;
        uint buttonCount;
        const char* axisNames[maxAxes];
        bool axisPresent[maxAxes];
        float axes[maxAxes];            // raw: sticks -1..1, XInput triggers 0..1
        bool down[maxButtons];
        bool pressed[maxButtons];       // true the frame the button goes down

        // the driving mapping, applied
        float steering;                 // -1..1, + = right
        float throttle;                 // -1..1, + = forward
        bool reset;                     // the reset button was pressed this frame

        bool IsXInput() const { return device >= controllerXInput && device < controllerXInput + XUSER_MAX_COUNT; }
    };

    ControllerSettings controllerSettings;
    Controller controller{};
    std::vector<ControllerDevice> controllerDevices;    // what the last scan found, XInput pads first
    std::atomic<bool> controllerRescan = true;          // set by WM_DEVICECHANGE, and on a read failure

    ControllerMapping& ActiveMapping()
    {
        return controller.IsXInput() ? controllerSettings.xinput : controllerSettings.joystick;
    }

    static void Narrow(const WCHAR* wide, char* out, int outSize)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, outSize, nullptr, nullptr);
        if (n <= 0) out[0] = 0;
        out[outSize - 1] = 0;
    }

    // winmm only gives "Microsoft PC-joystick driver"-style names in szPname; the product name
    // ("Radiomaster TX16S Joystick", ...) sits in the registry under the OEM key it points at.
    static bool JoystickOEMName(UINT id, const JOYCAPSW& caps, char* out, int outSize)
    {
        WCHAR path[256];
        swprintf(path, 256, L"System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\%s\\CurrentJoystickSettings", caps.szRegKey);
        WCHAR valueName[64];
        swprintf(valueName, 64, L"Joystick%uOEMName", id + 1);
        WCHAR oemKey[256];
        DWORD size = sizeof(oemKey);
        if (RegGetValueW(HKEY_CURRENT_USER, path, valueName, RRF_RT_REG_SZ, nullptr, oemKey, &size) != ERROR_SUCCESS &&
            RegGetValueW(HKEY_LOCAL_MACHINE, path, valueName, RRF_RT_REG_SZ, nullptr, oemKey, &size) != ERROR_SUCCESS)
            return false;
        swprintf(path, 256, L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\%s", oemKey);
        WCHAR name[128];
        size = sizeof(name);
        if (RegGetValueW(HKEY_CURRENT_USER, path, L"OEMName", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS &&
            RegGetValueW(HKEY_LOCAL_MACHINE, path, L"OEMName", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS)
            return false;
        Narrow(name, out, outSize);
        return out[0] != 0;
    }

    // Only on start-up, on WM_DEVICECHANGE, or when the device being read stops answering: polling an
    // empty XInput slot or an unplugged winmm id costs real time, so it is never done every frame.
    void ScanControllers()
    {
        ZoneScoped;
        controllerDevices.clear();

        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; slot++)
        {
            XINPUT_STATE state{};
            if (XInputGetState(slot, &state) != ERROR_SUCCESS)
                continue;
            ControllerDevice d{};
            d.device = controllerXInput + (int)slot;
            snprintf(d.name, sizeof(d.name), "Xbox controller %u (XInput)", slot + 1);
            controllerDevices.push_back(d);
        }

        joyConfigChanged(0);        // winmm caches its device list; pick up hot-plugged ones
        UINT count = joyGetNumDevs();
        for (UINT id = 0; id < count && id < 16; id++)
        {
            JOYINFOEX info{};
            info.dwSize = sizeof(info);
            info.dwFlags = JOY_RETURNALL;
            if (joyGetPosEx(id, &info) != JOYERR_NOERROR)
                continue;
            JOYCAPSW caps{};
            if (joyGetDevCapsW(id, &caps, sizeof(caps)) != JOYERR_NOERROR)
                continue;

            ControllerDevice d{};
            d.device = controllerJoystick + (int)id;
            char product[96];
            if (!JoystickOEMName(id, caps, product, sizeof(product)))
                Narrow(caps.szPname, product, sizeof(product));
            snprintf(d.name, sizeof(d.name), "%s (joystick %u)", product, id);
            d.hasAxis[0] = true;
            d.hasAxis[1] = true;
            d.hasAxis[2] = (caps.wCaps & JOYCAPS_HASZ) != 0;
            d.hasAxis[3] = (caps.wCaps & JOYCAPS_HASR) != 0;
            d.hasAxis[4] = (caps.wCaps & JOYCAPS_HASU) != 0;
            d.hasAxis[5] = (caps.wCaps & JOYCAPS_HASV) != 0;
            uint mins[6] = { caps.wXmin, caps.wYmin, caps.wZmin, caps.wRmin, caps.wUmin, caps.wVmin };
            uint maxs[6] = { caps.wXmax, caps.wYmax, caps.wZmax, caps.wRmax, caps.wUmax, caps.wVmax };
            for (uint a = 0; a < 6; a++) { d.axisMin[a] = mins[a]; d.axisMax[a] = maxs[a]; }
            d.buttonCount = caps.wNumButtons < Controller::maxButtons ? caps.wNumButtons : Controller::maxButtons;
            controllerDevices.push_back(d);
        }
    }

    const ControllerDevice* FindControllerDevice(int device) const
    {
        for (const ControllerDevice& d : controllerDevices)
            if (d.device == device)
                return &d;
        return nullptr;
    }

    bool ReadController(const ControllerDevice& d)
    {
        Controller& c = controller;
        bool downNow[Controller::maxButtons] = {};

        if (d.device >= controllerXInput && d.device < controllerXInput + XUSER_MAX_COUNT)
        {
            XINPUT_STATE state{};
            if (XInputGetState((DWORD)(d.device - controllerXInput), &state) != ERROR_SUCCESS)
                return false;
            const XINPUT_GAMEPAD& g = state.Gamepad;
            auto stick = [](SHORT v) { float f = (float)v / 32767.0f; return f < -1.0f ? -1.0f : f; };
            static const char* names[] = { "Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "LT", "RT", "RT - LT" };
            c.axisCount = 7;
            c.axes[0] = stick(g.sThumbLX);
            c.axes[1] = stick(g.sThumbLY);
            c.axes[2] = stick(g.sThumbRX);
            c.axes[3] = stick(g.sThumbRY);
            c.axes[4] = g.bLeftTrigger / 255.0f;
            c.axes[5] = g.bRightTrigger / 255.0f;
            c.axes[6] = c.axes[5] - c.axes[4];
            for (uint a = 0; a < c.axisCount; a++) { c.axisNames[a] = names[a]; c.axisPresent[a] = true; }

            // A B X Y LB RB Back Start LS RS Up Down Left Right
            static const WORD masks[] = { XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
                XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
                XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
                XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT };
            c.buttonCount = (uint)(sizeof(masks) / sizeof(masks[0]));
            for (uint b = 0; b < c.buttonCount; b++)
                downNow[b] = (g.wButtons & masks[b]) != 0;
        }
        else
        {
            JOYINFOEX info{};
            info.dwSize = sizeof(info);
            info.dwFlags = JOY_RETURNALL;
            if (joyGetPosEx((UINT)(d.device - controllerJoystick), &info) != JOYERR_NOERROR)
                return false;
            static const char* names[] = { "X", "Y", "Z", "R (Rx)", "U (Ry)", "V (Rz)" };
            DWORD raw[6] = { info.dwXpos, info.dwYpos, info.dwZpos, info.dwRpos, info.dwUpos, info.dwVpos };
            c.axisCount = 6;
            for (uint a = 0; a < 6; a++)
            {
                c.axisNames[a] = names[a];
                c.axisPresent[a] = d.hasAxis[a];
                float range = (float)d.axisMax[a] - (float)d.axisMin[a];
                c.axes[a] = d.hasAxis[a] && range > 0.0f ? ((float)raw[a] - (float)d.axisMin[a]) / range * 2.0f - 1.0f : 0.0f;
            }
            c.buttonCount = d.buttonCount;
            for (uint b = 0; b < c.buttonCount; b++)
                downNow[b] = (info.dwButtons & (1u << b)) != 0;
        }

        for (uint b = 0; b < Controller::maxButtons; b++)
        {
            c.pressed[b] = downNow[b] && !c.down[b];
            c.down[b] = downNow[b];
        }
        return true;
    }

    void UpdateController()
    {
        ZoneScoped;
        if (controllerRescan.exchange(false))
            ScanControllers();

        int device = controllerSettings.device;
        if (device == controllerAuto)
            device = controllerDevices.empty() ? -2 : controllerDevices[0].device;
        const ControllerDevice* d = FindControllerDevice(device);

        if (controller.device != device)
        {
            // switching devices: no button state carries over, so nothing reads as pressed or held
            memset(&controller, 0, sizeof(controller));
            controller.device = device;
        }

        if (!controllerSettings.enabled || d == nullptr || !ReadController(*d))
        {
            if (controller.connected && controllerSettings.enabled && d != nullptr)
                controllerRescan = true;    // it stopped answering -- unplugged, or the radio went off
            memset(&controller, 0, sizeof(controller));
            controller.device = device;
            return;
        }

        controller.connected = true;
        controller.device = device;
        snprintf(controller.name, sizeof(controller.name), "%s", d->name);

        const ControllerMapping& map = ActiveMapping();
        controller.steering = map.steering.Apply(controller.axes, controller.axisCount);
        controller.throttle = map.throttle.Apply(controller.axes, controller.axisCount);
        controller.reset = map.resetButton >= 0 && map.resetButton < (int)Controller::maxButtons && controller.pressed[map.resetButton];
    }

	Keys _keys{};
	Keys keys{};
	Mouse _mouse{};
	Mouse mouse{};
    WindowInformation window;

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

        UpdateController();

		// reset all inputs
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

		case WM_DEVICECHANGE:
		{
			// a pad, a radio or its dongle came or went -- rescan on the next Update
			pThis->controllerRescan = true;
			return DefWindowProc(hwnd, umessage, wparam, lparam);
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

	void On()
	{
		ZoneScoped;

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
			
			int px = (int)(((float)screenSize.x - (float)window.windowResolution.x) * 0.5f);
			int py = (int)(((float)screenSize.y - (float)window.windowResolution.y) * 0.5f);
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

	static void Log(std::string& str)
	{
		std::cout << str << "\n";
		OutputDebugStringA(str.c_str());
	}
	template<typename... Args>
	static void Log(std::string_view rt_fmt_str, Args&&... args)
	{
		std::string message = std::vformat(rt_fmt_str, std::make_format_args(args...));
		message.append("\n");
		std::cout << message;
		OutputDebugStringA(message.c_str());
	}

	static void Log(std::wstring& str)
	{
		std::wcout << str << "\n";
		OutputDebugStringW(str.c_str());
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