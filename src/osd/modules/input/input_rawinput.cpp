// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Brad Hughes, Chester C. Rumpled
//============================================================
//
//  input_rawinput.cpp - Windows RawInput input implementation
//
//============================================================

#include "modules/osdmodule.h"

#if defined(OSD_WINDOWS)

#include "input_windows.h"

#include "input_wincommon.h"

#include "winmain.h"
#include "window.h"

#include "modules/lib/osdlib.h"
#include "strconv.h"

// MAME headers
#include "inpttype.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <mutex>
#include <new>

// standard windows headers
#include <windows.h>
#include <tchar.h>
extern "C"
{
	#include <hidsdi.h>
}

namespace osd {

namespace {

class safe_regkey
{
private:
	HKEY m_key;

public:
	safe_regkey() : m_key(nullptr) { }
	safe_regkey(safe_regkey const &) = delete;
	safe_regkey(safe_regkey &&key) : m_key(key.m_key) { key.m_key = nullptr; }
	explicit safe_regkey(HKEY key) : m_key(key) { }

	~safe_regkey() { close(); }

	safe_regkey &operator=(safe_regkey const &) = delete;

	safe_regkey &operator=(safe_regkey &&key)
	{
		close();
		m_key = key.m_key;
		key.m_key = nullptr;
		return *this;
	}

	explicit operator bool() const { return m_key != nullptr; }

	void close()
	{
		if (m_key != nullptr)
		{
			RegCloseKey(m_key);
			m_key = nullptr;
		}
	}

	operator HKEY() const { return m_key; }

	safe_regkey open(std::wstring const &subkey) const { return open(m_key, subkey); }

	std::wstring enum_key(int index) const
	{
		WCHAR keyname[256];
		DWORD namelen = std::size(keyname);
		if (RegEnumKeyEx(m_key, index, keyname, &namelen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
			return std::wstring(keyname, namelen);
		else
			return std::wstring();
	}

	std::wstring query_string(WCHAR const *path) const
	{
		// first query to get the length
		DWORD datalen;
		if (RegQueryValueExW(m_key, path, nullptr, nullptr, nullptr, &datalen) != ERROR_SUCCESS)
			return std::wstring();

		// allocate a buffer
		auto buffer = std::make_unique<WCHAR []>((datalen + (sizeof(WCHAR) * 2) - 1) / sizeof(WCHAR));

		// now get the actual data
		if (RegQueryValueExW(m_key, path, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.get()), &datalen) != ERROR_SUCCESS)
			return std::wstring();

		buffer[datalen / sizeof(WCHAR)] = 0;
		return std::wstring(buffer.get());
	}

	template <typename T> void foreach_subkey(T &&action) const
	{
		std::wstring name;
		for (int i = 0; ; i++)
		{
			name = enum_key(i);
			if (name.empty())
				break;

			safe_regkey const subkey = open(name);
			if (!subkey)
				break;

			bool const shouldcontinue = action(subkey);
			if (!shouldcontinue)
				break;
		}
	}

	static safe_regkey open(HKEY basekey, std::wstring const &subkey)
	{
		HKEY key(nullptr);
		if (RegOpenKeyEx(basekey, subkey.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
			return safe_regkey(key);
		else
			return safe_regkey();
	}
};

std::wstring trim_prefix(const std::wstring &devicename)
{
	// remove anything prior to the final semicolon
	auto semicolon_index = devicename.find_last_of(';');
	if (semicolon_index != std::wstring::npos)
		return devicename.substr(semicolon_index + 1);

	return devicename;
}

std::wstring compute_device_regpath(const std::wstring &name)
{
	static const std::wstring basepath(L"SYSTEM\\CurrentControlSet\\Enum\\");

	// allocate a temporary string and concatenate the base path plus the name
	auto regpath_buffer = std::make_unique<WCHAR []>(basepath.length() + 1 + name.length());
	wcscpy(regpath_buffer.get(), basepath.c_str());
	WCHAR *chdst = regpath_buffer.get() + basepath.length();

	// convert all # to \ in the name
	for (int i = 4; i < name.length(); i++)
		*chdst++ = (name[i] == '#') ? L'\\' : name[i];
	*chdst = 0;

	// remove the final chunk
	chdst = wcsrchr(regpath_buffer.get(), L'\\');
	if (chdst == nullptr)
		return std::wstring();

	*chdst = 0;

	return std::wstring(regpath_buffer.get());
}

std::wstring improve_name_from_base_path(const std::wstring &regpath, bool *hid)
{
	// now try to open the registry key
	auto device_key = safe_regkey::open(HKEY_LOCAL_MACHINE, regpath);
	if (!device_key)
		return std::wstring();

	// fetch the device description; if it exists, we are finished
	auto regstring = device_key.query_string(L"DeviceDesc");
	if (!regstring.empty())
		return trim_prefix(regstring);

	// if the key name does not contain "HID", it's not going to be in the USB tree; give up
	*hid = regpath.find(L"HID") != std::string::npos;
	return std::wstring();
}

std::wstring improve_name_from_usb_path(const std::wstring &regpath)
{
	static const std::wstring usbbasepath(L"SYSTEM\\CurrentControlSet\\Enum\\USB");

	// extract the expected parent ID from the regpath
	size_t last_slash_index = regpath.find_last_of('\\');
	if (last_slash_index == std::wstring::npos)
		return std::wstring();

	std::wstring parentid = regpath.substr(last_slash_index + 1);

	// open the USB key
	auto usb_key = safe_regkey::open(HKEY_LOCAL_MACHINE, usbbasepath);
	if (!usb_key)
		return std::wstring();

	std::wstring regstring;

	usb_key.foreach_subkey(
			[&regstring, &parentid] (safe_regkey const &subkey)
			{
				subkey.foreach_subkey(
						[&regstring, &parentid] (safe_regkey const &endkey)
						{
							std::wstring endparentid = endkey.query_string(L"ParentIdPrefix");

							// This key doesn't have a ParentIdPrefix
							if (endparentid.empty())
								return true;

							// do we have a match?
							if (parentid.find(endparentid) == 0)
								regstring = endkey.query_string(L"DeviceDesc");

							return regstring.empty();
						});

				return regstring.empty();
			});

	return trim_prefix(regstring);
}


//============================================================
//  rawinput_device_improve_name
//============================================================

std::wstring rawinput_device_improve_name(const std::wstring &name)
{
	// The RAW name received is formatted as:
	//   \??\type-id#hardware-id#instance-id#{DeviceClasses-id}
	// XP starts with "\??\"
	// Vista64 starts with "\\?\"

	// ensure the name is something we can handle
	if (name.find(L"\\\\?\\") != 0 && name.find(L"\\??\\") != 0)
		return name;

	std::wstring regpath = compute_device_regpath(name);

	bool hid = false;
	auto improved = improve_name_from_base_path(regpath, &hid);
	if (!improved.empty())
		return improved;

	if (hid)
	{
		improved = improve_name_from_usb_path(regpath);
		if (!improved.empty())
			return improved;
	}

	// Fall back to the original name
	return name;
}


//============================================================
//  rawinput_device class
//============================================================

class rawinput_device : public event_based_device<RAWINPUT>
{
public:
	rawinput_device(std::string &&name, std::string &&id, input_module &module, HANDLE handle) :
		event_based_device(std::move(name), std::move(id), module),
		m_handle(handle)
	{
	}

	HANDLE device_handle() const { return m_handle; }

	bool reconnect_candidate(std::string_view i) const { return !m_handle && (id() == i); }

	void detach_device()
	{
		assert(m_handle);
		m_handle = nullptr;
		osd_printf_verbose("RawInput: %s [ID %s] disconnected\n", name(), id());
	}

	void attach_device(HANDLE handle)
	{
		assert(!m_handle);
		m_handle = handle;
		osd_printf_verbose("RawInput: %s [ID %s] reconnected\n", name(), id());
	}

private:
	HANDLE m_handle;
};


//============================================================
//  rawinput_keyboard_device
//============================================================

class rawinput_keyboard_device : public rawinput_device
{
public:
	rawinput_keyboard_device(std::string &&name, std::string &&id, input_module &module, HANDLE handle) :
		rawinput_device(std::move(name), std::move(id), module, handle),
		m_keyboard({ { 0 } })
	{
	}

	virtual void reset() override
	{
		memset(&m_keyboard, 0, sizeof(m_keyboard));
	}

	virtual void process_event(RAWINPUT const &rawinput) override
	{
		// determine the full DIK-compatible scancode
		uint8_t scancode = (rawinput.data.keyboard.MakeCode & 0x7f) | ((rawinput.data.keyboard.Flags & RI_KEY_E0) ? 0x80 : 0x00);

		// scancode 0xaa is a special shift code we need to ignore
		if (scancode == 0xaa)
			return;

		// set or clear the key
		m_keyboard.state[scancode] = (rawinput.data.keyboard.Flags & RI_KEY_BREAK) ? 0x00 : 0x80;
	}

	virtual void configure(input_device &device) override
	{
		keyboard_trans_table const &table = keyboard_trans_table::instance();

		for (int keynum = 0; keynum < MAX_KEYS; keynum++)
		{
			input_item_id itemid = table.map_di_scancode_to_itemid(keynum);
			WCHAR keyname[100];

			// generate the name
			if (GetKeyNameTextW(((keynum & 0x7f) << 16) | ((keynum & 0x80) << 17), keyname, std::size(keyname)) == 0)
				_snwprintf(keyname, std::size(keyname), L"Scan%03d", keynum);
			std::string name = text::from_wstring(keyname);

			// add the item to the device
			device.add_item(
					name,
					util::string_format("SCAN%03d", keynum),
					itemid,
					generic_button_get_state<std::uint8_t>,
					&m_keyboard.state[keynum]);
		}
	}

private:
	keyboard_state m_keyboard;
};


//============================================================
//  rawinput_joystick_device
//============================================================

class rawinput_joystick_device : public rawinput_device
{
public:
	rawinput_joystick_device(std::string &&name, std::string &&id, input_module &module, HANDLE handle) :
		rawinput_device(std::move(name), std::move(id), module, handle),
		m_joystick({ { 0 } })
	{
	}

	virtual void reset() override
	{
		memset(&m_joystick, 0, sizeof(m_joystick));
	}

	virtual void process_event(RAWINPUT const &rawinput) override
	{
		for (size_t button_index = 0; button_index != MAX_BUTTONS; ++button_index)
			m_joystick.buttons[button_index] = 0;

		for (size_t axis_index = 0; axis_index != 9; ++axis_index)
			m_joystick.axes[axis_index] = 0;

		for (size_t hat_index = 0; hat_index != 4; ++hat_index)
			m_joystick.hats[hat_index] = 0;

		UINT preparsed_data_buf_size = 0;
		if (GetRawInputDeviceInfo(rawinput.header.hDevice, RIDI_PREPARSEDDATA, NULL, &preparsed_data_buf_size) != 0)
			return;

		std::unique_ptr<uint8_t[]> preparsed_data_buf = std::make_unique<uint8_t[]>(preparsed_data_buf_size);
		PHIDP_PREPARSED_DATA preparsed_data_buf_ptr = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed_data_buf.get());

		if (GetRawInputDeviceInfo(rawinput.header.hDevice, RIDI_PREPARSEDDATA, preparsed_data_buf_ptr, &preparsed_data_buf_size) < 0)
			return;

		HIDP_CAPS joystick_caps;
		if (HidP_GetCaps(preparsed_data_buf_ptr, &joystick_caps) != HIDP_STATUS_SUCCESS)
			return;

		set_button_caps(rawinput, preparsed_data_buf_ptr, joystick_caps.NumberInputButtonCaps);
		set_value_caps(rawinput, preparsed_data_buf_ptr, joystick_caps.NumberInputValueCaps);
	}

	virtual void configure(input_device &device) override
	{
		// dual shock 4 and dual sense gamepads have bi-directional triggers and don't behave the same as other axes;
		// their released state is 100% negative
		RID_DEVICE_INFO rdi = {};
		rdi.cbSize = sizeof(RID_DEVICE_INFO);
		UINT rdi_size = rdi.cbSize;
		GetRawInputDeviceInfoW(device_handle(), RIDI_DEVICEINFO, &rdi, &rdi_size);

		if (rdi.hid.dwVendorId == 0x054C) // Sony vendor ID
		{
			switch (rdi.hid.dwProductId)
			{
				case 0x05C4:    // dualShock4Gen1ProductId
				case 0x09CC:    // dualShock4Gen2ProductId
				case 0x0CE6:    // dualSenseProductId
				{
					m_joystick.bidirectional_trigger_axis[3] = true;
					m_joystick.bidirectional_trigger_axis[4] = true;
					break;
				}
				default:
				{
					break;
				}
			}
		}

		// populate it
		const char *const rawinput_pov_names[] = {"DPAD Up", "DPAD Down", "DPAD Left", "DPAD Right"};

		for (size_t pov_index = 0; pov_index != 4; ++pov_index)
			device.add_item(
					rawinput_pov_names[pov_index],
					std::string_view(),
					ITEM_ID_OTHER_SWITCH,
					generic_button_get_state<int32_t>,
					&m_joystick.hats[pov_index]);

		// loop over all axes
		for (int axis = 0; axis != 9; ++axis)
		{
			char temp_name[512];
			input_item_id itemid;

			if (axis < INPUT_MAX_AXIS)
				itemid = (input_item_id)(ITEM_ID_XAXIS + axis);
			else if (axis < INPUT_MAX_AXIS + INPUT_MAX_ADD_ABSOLUTE)
				itemid = (input_item_id)(ITEM_ID_ADD_ABSOLUTE1 - INPUT_MAX_AXIS + axis);
			else
				itemid = ITEM_ID_OTHER_AXIS_ABSOLUTE;

			snprintf(temp_name, sizeof(temp_name), "A%d", axis + 1);
			device.add_item(
					temp_name,
					std::string_view(),
					itemid,
					generic_axis_get_state<std::int32_t>,
					&m_joystick.axes[axis]);
		}

		// add the item to the device
		for (size_t button_index = 0; button_index != MAX_BUTTONS; ++button_index)
			device.add_item(default_button_name(button_index),
					std::string_view(),
					static_cast<input_item_id>(ITEM_ID_BUTTON1 + button_index),
					generic_button_get_state<std::int32_t>,
					&m_joystick.buttons[button_index]);
	}

private:
	joystick_state m_joystick;

	void set_axis_value(const ULONG usage_value, const HIDP_VALUE_CAPS& value_cap, const size_t axis_index)
	{
		const unsigned long bitmask = (1  << value_cap.BitSize) - 1;
		const double current_value = static_cast<double>(usage_value & bitmask);

		if (m_joystick.bidirectional_trigger_axis[axis_index] == true && current_value == 0.0)
			return;

		const double min_value = static_cast<double>(value_cap.LogicalMin & bitmask);
		const double max_value = static_cast<double>(value_cap.LogicalMax & bitmask);

		m_joystick.axes[axis_index] = normalize_absolute_axis(current_value, min_value, max_value);
	}

	void set_value_caps(RAWINPUT const &rawinput, const PHIDP_PREPARSED_DATA& preparsed_data_buf_ptr, USHORT number_input_value_caps)
	{
		if (number_input_value_caps < 1)
			return;

		std::unique_ptr<HIDP_VALUE_CAPS[]> value_caps(new HIDP_VALUE_CAPS[number_input_value_caps]);

		if (HidP_GetValueCaps(HidP_Input, value_caps.get(), reinterpret_cast<PUSHORT>(&number_input_value_caps), preparsed_data_buf_ptr) != HIDP_STATUS_SUCCESS)
			return;

		for (size_t value_cap_index = 0; value_cap_index != number_input_value_caps; ++value_cap_index)
		{
			const HIDP_VALUE_CAPS& value_cap = value_caps[value_cap_index];

			ULONG usage_value;
			if (HidP_GetUsageValue(HidP_Input, value_cap.UsagePage, 0, value_cap.Range.UsageMin, &usage_value, preparsed_data_buf_ptr,
									(PCHAR)(rawinput.data.hid.bRawData), rawinput.data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS)
				continue;

			switch (value_cap.Range.UsageMin)
			{
				case HID_USAGE_GENERIC_X:
				case HID_USAGE_GENERIC_Y:
				case HID_USAGE_GENERIC_Z:
				case HID_USAGE_GENERIC_RX:
				case HID_USAGE_GENERIC_RY:
				case HID_USAGE_GENERIC_RZ:
				case HID_USAGE_GENERIC_SLIDER:
				case HID_USAGE_GENERIC_DIAL:
				case HID_USAGE_GENERIC_WHEEL:
				{
					set_axis_value(usage_value, value_cap, value_cap.Range.UsageMin - HID_USAGE_GENERIC_X);

					break;
				}
				case HID_USAGE_GENERIC_HATSWITCH:
				{
					const LONG hat_value = usage_value - value_cap.LogicalMin;

					m_joystick.hats[0] = (hat_value == 0 || hat_value == 1 || hat_value == 7) ? 0x80 : 0;
					m_joystick.hats[1] = (hat_value == 3 || hat_value == 4 || hat_value == 5) ? 0x80 : 0;
					m_joystick.hats[2] = (hat_value == 5 || hat_value == 6 || hat_value == 7) ? 0x80 : 0;
					m_joystick.hats[3] = (hat_value == 1 || hat_value == 2 || hat_value == 3) ? 0x80 : 0;

					break;
				}
				default:

					break;
			}
		}
	}

	void set_button_caps(RAWINPUT const &rawinput, const PHIDP_PREPARSED_DATA& preparsed_data_buf_ptr, USHORT number_input_button_caps)
	{
		if (number_input_button_caps < 1)
			return;

		std::vector<HIDP_BUTTON_CAPS> button_caps(number_input_button_caps);

		if (HidP_GetButtonCaps(HidP_Input, button_caps.data(), reinterpret_cast<PUSHORT>(&number_input_button_caps), preparsed_data_buf_ptr) != HIDP_STATUS_SUCCESS)
			return;

		ULONG usageLength = button_caps.data()->Range.UsageMax - button_caps.data()->Range.UsageMin + 1;

		if (usageLength < 1)
			return;

		std::unique_ptr<USAGE[]> usages = std::make_unique<USAGE[]>(usageLength);

		if (HidP_GetUsages(HidP_Input, button_caps.data()->UsagePage, 0, usages.get(), &usageLength, preparsed_data_buf_ptr,
							(PCHAR)(rawinput.data.hid.bRawData), rawinput.data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS)
			return;

		for (size_t usageIndex = 0; usageIndex != usageLength; ++usageIndex)
		{
			const size_t button_index = static_cast<size_t>(usages[usageIndex]) - static_cast<size_t>(button_caps.data()->Range.UsageMin);
			m_joystick.buttons[button_index] = 0x80;
		}
	}
};


//============================================================
//  rawinput_mouse_device
//============================================================

class rawinput_mouse_device : public rawinput_device
{
public:
	rawinput_mouse_device(std::string &&name, std::string &&id, input_module &module, HANDLE handle) :
		rawinput_device(std::move(name), std::move(id), module, handle),
		m_mouse({0}),
		m_x(0),
		m_y(0),
		m_z(0)
	{
	}

	virtual void poll(bool relative_reset) override
	{
		rawinput_device::poll(relative_reset);
		if (relative_reset)
		{
			m_mouse.lX = std::exchange(m_x, 0);
			m_mouse.lY = std::exchange(m_y, 0);
			m_mouse.lZ = std::exchange(m_z, 0);
		}
	}

	virtual void reset() override
	{
		memset(&m_mouse, 0, sizeof(m_mouse));
		m_x = m_y = m_z = 0;
	}

	virtual void configure(input_device &device) override
	{
		// populate the axes
		for (int axisnum = 0; axisnum < 3; axisnum++)
		{
			device.add_item(
					default_axis_name[axisnum],
					std::string_view(),
					input_item_id(ITEM_ID_XAXIS + axisnum),
					generic_axis_get_state<LONG>,
					&m_mouse.lX + axisnum);
		}

		// populate the buttons
		for (int butnum = 0; butnum < 5; butnum++)
		{
			device.add_item(
					default_button_name(butnum),
					std::string_view(),
					input_item_id(ITEM_ID_BUTTON1 + butnum),
					generic_button_get_state<BYTE>,
					&m_mouse.rgbButtons[butnum]);
		}
	}

	virtual void process_event(RAWINPUT const &rawinput) override
	{
		// If this data was intended for a rawinput mouse
		if (rawinput.data.mouse.usFlags == MOUSE_MOVE_RELATIVE)
		{

			m_x += rawinput.data.mouse.lLastX * input_device::RELATIVE_PER_PIXEL;
			m_y += rawinput.data.mouse.lLastY * input_device::RELATIVE_PER_PIXEL;

			// update Z axis (vertical scroll)
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
				m_z += int16_t(rawinput.data.mouse.usButtonData) * input_device::RELATIVE_PER_PIXEL;

			// update the button states; always update the corresponding mouse buttons
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) m_mouse.rgbButtons[0] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)   m_mouse.rgbButtons[0] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) m_mouse.rgbButtons[1] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)   m_mouse.rgbButtons[1] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) m_mouse.rgbButtons[2] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP)   m_mouse.rgbButtons[2] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) m_mouse.rgbButtons[3] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)   m_mouse.rgbButtons[3] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) m_mouse.rgbButtons[4] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)   m_mouse.rgbButtons[4] = 0x00;
		}
	}

private:
	mouse_state m_mouse;
	LONG m_x, m_y, m_z;
};


//============================================================
//  rawinput_lightgun_device
//============================================================

class rawinput_lightgun_device : public rawinput_device
{
public:
	rawinput_lightgun_device(std::string &&name, std::string &&id, input_module &module, HANDLE handle) :
		rawinput_device(std::move(name), std::move(id), module, handle),
		m_lightgun({0}),
		m_z(0)
	{
	}

	virtual void poll(bool relative_reset) override
	{
		rawinput_device::poll(relative_reset);
		if (relative_reset)
			m_lightgun.lZ = std::exchange(m_z, 0);
	}

	virtual void reset() override
	{
		memset(&m_lightgun, 0, sizeof(m_lightgun));
		m_z = 0;
	}

	virtual void configure(input_device &device) override
	{
		// populate the axes
		for (int axisnum = 0; axisnum < 2; axisnum++)
		{
			device.add_item(
					default_axis_name[axisnum],
					std::string_view(),
					input_item_id(ITEM_ID_XAXIS + axisnum),
					generic_axis_get_state<LONG>,
					&m_lightgun.lX + axisnum);
		}

		// scroll wheel is always relative if present
		device.add_item(
				default_axis_name[2],
				std::string_view(),
				ITEM_ID_ADD_RELATIVE1,
				generic_axis_get_state<LONG>,
				&m_lightgun.lZ);

		// populate the buttons
		for (int butnum = 0; butnum < 5; butnum++)
		{
			device.add_item(
					default_button_name(butnum),
					std::string_view(),
					input_item_id(ITEM_ID_BUTTON1 + butnum),
					generic_button_get_state<BYTE>,
					&m_lightgun.rgbButtons[butnum]);
		}
	}

	virtual void process_event(RAWINPUT const &rawinput) override
	{
		// If this data was intended for a rawinput lightgun
		if (rawinput.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
		{

			// update the X/Y positions
			m_lightgun.lX = normalize_absolute_axis(rawinput.data.mouse.lLastX, 0, input_device::ABSOLUTE_MAX);
			m_lightgun.lY = normalize_absolute_axis(rawinput.data.mouse.lLastY, 0, input_device::ABSOLUTE_MAX);

			// update zaxis
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
				m_z += int16_t(rawinput.data.mouse.usButtonData) * input_device::RELATIVE_PER_PIXEL;

			// update the button states; always update the corresponding mouse buttons
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) m_lightgun.rgbButtons[0] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)   m_lightgun.rgbButtons[0] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) m_lightgun.rgbButtons[1] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)   m_lightgun.rgbButtons[1] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) m_lightgun.rgbButtons[2] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP)   m_lightgun.rgbButtons[2] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) m_lightgun.rgbButtons[3] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)   m_lightgun.rgbButtons[3] = 0x00;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) m_lightgun.rgbButtons[4] = 0x80;
			if (rawinput.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)   m_lightgun.rgbButtons[4] = 0x00;
		}
	}

private:
	mouse_state m_lightgun;
	LONG m_z;
};


//============================================================
//  rawinput_module - base class for rawinput modules
//============================================================

class rawinput_module : public wininput_module<rawinput_device>
{
private:
	std::mutex  m_module_lock;

public:
	rawinput_module(const char *type, const char *name) : wininput_module<rawinput_device>(type, name)
	{
	}

	virtual bool probe() override
	{
		return true;
	}

	virtual void input_init(running_machine &machine) override
	{
		wininput_module<rawinput_device>::input_init(machine);

		// get initial number of devices
		UINT device_count = 0;
		if (GetRawInputDeviceList(nullptr, &device_count, sizeof(RAWINPUTDEVICELIST)) != 0)
		{
			osd_printf_error("Error getting initial number of RawInput devices.\n");
			return;
		}
		if (!device_count)
			return;

		std::unique_ptr<RAWINPUTDEVICELIST []> rawinput_devices;
		UINT retrieved;
		do
		{
			rawinput_devices.reset(new (std::nothrow) RAWINPUTDEVICELIST [device_count]);
			if (!rawinput_devices)
			{
				osd_printf_error("Error allocating buffer for RawInput device list.\n");
				return;
			}
			retrieved = GetRawInputDeviceList(rawinput_devices.get(), &device_count, sizeof(RAWINPUTDEVICELIST));
		}
		while ((UINT(-1) == retrieved) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER));
		if (UINT(-1) == retrieved)
		{
			osd_printf_error("Error listing RawInput devices.\n");
			return;
		}

		// iterate backwards through devices; new devices are added at the head
		for (int devnum = retrieved - 1; devnum >= 0; devnum--)
			add_rawinput_device(rawinput_devices[devnum]);

		// If we added no devices, no need to register for notifications
		if (devicelist().empty())
			return;

		// finally, register to receive raw input WM_INPUT messages if we found devices
		RAWINPUTDEVICE registration;
		registration.usUsagePage = HID_USAGE_PAGE_GENERIC;
		registration.usUsage = usage();
		registration.dwFlags = RIDEV_DEVNOTIFY;
		if (background_input())
			registration.dwFlags |= RIDEV_INPUTSINK;
		registration.hwndTarget = dynamic_cast<win_window_info &>(*osd_common_t::window_list().front()).platform_window();

		// some joysticks are reported as gamepads and viceversa, so we register both
		std::vector<RAWINPUTDEVICE> registrations;
		registrations.push_back(registration);

		if (registration.usUsage == HID_USAGE_GENERIC_JOYSTICK)
		{
			registration.usUsage = HID_USAGE_GENERIC_GAMEPAD;
			registrations.push_back(registration);
		}

		if (!RegisterRawInputDevices(registrations.data(), registrations.size(), sizeof(registration)))
			osd_printf_error("Error registering RawInput devices.\n");
	}

protected:
	virtual void add_rawinput_device(RAWINPUTDEVICELIST const &device) = 0;
	virtual USHORT usage() = 0;

	template<class TDevice>
	TDevice *create_rawinput_device(input_device_class deviceclass, RAWINPUTDEVICELIST const &rawinputdevice)
	{
		// determine the length of the device name, allocate it, and fetch it if not nameless
		UINT name_length = 0;
		if (GetRawInputDeviceInfoW(rawinputdevice.hDevice, RIDI_DEVICENAME, nullptr, &name_length) != 0)
			return nullptr;

		std::unique_ptr<WCHAR []> tname = std::make_unique<WCHAR []>(name_length + 1);
		if (name_length > 1 && GetRawInputDeviceInfoW(rawinputdevice.hDevice, RIDI_DEVICENAME, tname.get(), &name_length) == UINT(-1))
			return nullptr;

		const std::wstring tname_basic_string = tname.get();
		tname.reset();

		// if this is an RDP name, skip it
		if (tname_basic_string.find(L"Root#RDP_") != std::string::npos)
			return nullptr;

		// this is for duplicate devices in a collection such as extra mouse buttons
		if (tname_basic_string.find(L"&Col01") != std::string::npos)
			return nullptr;

		// set device ID to raw input name
		std::string utf8_id = osd::text::from_wstring(tname_basic_string);
		std::string utf8_name;

		HANDLE hid_handle = CreateFileW(tname_basic_string.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);

		if (hid_handle == INVALID_HANDLE_VALUE)
		{
			GetLastError();

			// improve the name
			utf8_name = osd::text::from_wstring(rawinput_device_improve_name(tname_basic_string));
		}
		else
		{
			std::wstring buffer;
			buffer.resize(256);

			if (HidD_GetProductString(hid_handle, buffer.data(), static_cast<ULONG>(buffer.size())))
			{
				buffer.erase(buffer.find(L'\0'));
				utf8_name = osd::text::from_wstring(buffer);
			}

			buffer.resize(256);

			if (HidD_GetManufacturerString(hid_handle, buffer.data(), static_cast<ULONG>(buffer.size())))
			{
				buffer.erase(buffer.find(L'\0'));

				if (buffer != L"")
					utf8_name += " (" + osd::text::from_wstring(buffer) + ")";
			}
		}

		CloseHandle(hid_handle);

		// allocate a device
		return &create_device<TDevice>(
				deviceclass,
				std::move(utf8_name),
				std::move(utf8_id),
				rawinputdevice.hDevice);
	}

	virtual bool handle_input_event(input_event eventid, void *eventdata) override
	{
		switch (eventid)
		{
		// handle raw input data
		case INPUT_EVENT_RAWINPUT:
			{
				HRAWINPUT const rawinputdevice = *static_cast<HRAWINPUT *>(eventdata);

				BYTE small_buffer[4096];
				std::unique_ptr<BYTE []> larger_buffer;
				LPBYTE data = small_buffer;
				UINT size;

				// determine the size of data buffer we need
				if (GetRawInputData(rawinputdevice, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
					return false;

				// if necessary, allocate a temporary buffer and fetch the data
				if (size > sizeof(small_buffer))
				{
					larger_buffer.reset(new (std::nothrow) BYTE [size]);
					data = larger_buffer.get();
					if (!data)
						return false;
				}

				// fetch the data and process the appropriate message types
				UINT result = GetRawInputData(rawinputdevice, RID_INPUT, data, &size, sizeof(RAWINPUTHEADER));
				if (UINT(-1) == result)
				{
					return false;
				}
				else if (result)
				{
					std::lock_guard<std::mutex> scope_lock(m_module_lock);

					auto *input = reinterpret_cast<RAWINPUT *>(data);
					if (!input->header.hDevice)
						return false;

					// find the device in the list and update
					auto target_device = std::find_if(
							devicelist().begin(),
							devicelist().end(),
							[input] (auto const &device)
							{
								return input->header.hDevice == device->device_handle();
							});
					if (devicelist().end() == target_device)
						return false;

					(*target_device)->queue_events(input, 1);
					return true;
				}
			}
			break;

		case INPUT_EVENT_ARRIVAL:
			{
				HRAWINPUT const rawinputdevice = *static_cast<HRAWINPUT *>(eventdata);

				// determine the length of the device name, allocate it, and fetch it if not nameless
				UINT name_length = 0;
				if (GetRawInputDeviceInfoW(rawinputdevice, RIDI_DEVICENAME, nullptr, &name_length) != 0)
					return false;

				std::unique_ptr<WCHAR []> tname = std::make_unique<WCHAR []>(name_length + 1);
				if (name_length > 1 && GetRawInputDeviceInfoW(rawinputdevice, RIDI_DEVICENAME, tname.get(), &name_length) == UINT(-1))
					return false;
				std::string utf8_id = text::from_wstring(tname.get());
				tname.reset();

				std::lock_guard<std::mutex> scope_lock(m_module_lock);

				// find the device in the list and update
				auto target_device = std::find_if(
						devicelist().begin(),
						devicelist().end(),
						[&utf8_id] (auto const &device)
						{
							return device->reconnect_candidate(utf8_id);
						});
				if (devicelist().end() == target_device)
					return false;

				(*target_device)->attach_device(rawinputdevice);
				return true;
			}
			break;

		case INPUT_EVENT_REMOVAL:
			{
				HRAWINPUT const rawinputdevice = *static_cast<HRAWINPUT *>(eventdata);

				std::lock_guard<std::mutex> scope_lock(m_module_lock);

				// find the device in the list and update
				auto target_device = std::find_if(
						devicelist().begin(),
						devicelist().end(),
						[rawinputdevice] (auto const &device)
						{
							return rawinputdevice == device->device_handle();
						});

				if (devicelist().end() == target_device)
					return false;

				(*target_device)->reset();
				(*target_device)->detach_device();
				return true;
			}
			break;

		default:
			break;
		}

		// must have been unhandled
		return false;
	}
};


//============================================================
//  keyboard_input_rawinput - rawinput keyboard module
//============================================================

class keyboard_input_rawinput : public rawinput_module
{
public:
	keyboard_input_rawinput() : rawinput_module(OSD_KEYBOARDINPUT_PROVIDER, "rawinput")
	{
	}

protected:
	virtual USHORT usage() override { return HID_USAGE_GENERIC_KEYBOARD; }

	virtual void add_rawinput_device(RAWINPUTDEVICELIST const &device) override
	{
		// make sure this is a keyboard
		if (device.dwType != RIM_TYPEKEYBOARD)
			return;

		// allocate and link in a new device
		create_rawinput_device<rawinput_keyboard_device>(DEVICE_CLASS_KEYBOARD, device);
	}
};


//============================================================
//  joystick_input_rawinput - rawinput joystick module
//============================================================

class joystick_input_rawinput : public rawinput_module
{
public:
	joystick_input_rawinput() : rawinput_module(OSD_JOYSTICKINPUT_PROVIDER, "rawinput")
	{
	}

protected:
	virtual USHORT usage() override { return HID_USAGE_GENERIC_JOYSTICK; }

	void add_rawinput_device(RAWINPUTDEVICELIST const &device) override
	{
		// first make sure this is not a keyboard or a mouse
		if (device.dwType != RIM_TYPEHID)
			return;

		// also check if it's a valid joystick or gamepad
		if (!is_valid_joystick(device))
			return;

		// allocate and link in a new device
		create_rawinput_device<rawinput_joystick_device>(DEVICE_CLASS_JOYSTICK, device);

	}

	bool is_valid_joystick(RAWINPUTDEVICELIST const &device)
	{
		RID_DEVICE_INFO rdi = {};
		rdi.cbSize = sizeof(RID_DEVICE_INFO);

		UINT rdi_size = rdi.cbSize;
		if (GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICEINFO, &rdi, &rdi_size) < 1)
			return false;

		if (rdi.hid.usUsage != HID_USAGE_GENERIC_JOYSTICK && rdi.hid.usUsage != HID_USAGE_GENERIC_GAMEPAD)
			return false;

		// get the device information
		UINT preparsed_data_buffer_size;
		GetRawInputDeviceInfoW(device.hDevice, RIDI_PREPARSEDDATA, NULL, &preparsed_data_buffer_size);

		std::unique_ptr<uint8_t[]> preparsed_data_buffer = std::make_unique<uint8_t[]>(preparsed_data_buffer_size);
		PHIDP_PREPARSED_DATA preparsed_data_ptr = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed_data_buffer.get());
		if (GetRawInputDeviceInfoW(device.hDevice, RIDI_PREPARSEDDATA, preparsed_data_ptr, &preparsed_data_buffer_size) < 0)
			return false;

		HIDP_CAPS joystick_capabilities;
		if (HidP_GetCaps(preparsed_data_ptr, &joystick_capabilities) != HIDP_STATUS_SUCCESS)
			return false;

		if (joystick_capabilities.NumberInputButtonCaps < 1 && joystick_capabilities.NumberInputValueCaps < 1)
			return false;

		std::vector<HIDP_BUTTON_CAPS> button_capabilities(joystick_capabilities.NumberInputButtonCaps);
		if (HidP_GetButtonCaps(HidP_Input, button_capabilities.data(), &joystick_capabilities.NumberInputButtonCaps, preparsed_data_ptr) != HIDP_STATUS_SUCCESS)
			return false;

		// Populate joystick device buttons
		constexpr uint32_t button_usage_page = HID_USAGE_PAGE_BUTTON;
		constexpr size_t buttons_length_cap = 32;
		size_t button_count = 0;

		for (const auto& button_capability : button_capabilities)
		{
			uint16_t usage_min = button_capability.Range.UsageMin;
			uint16_t usage_max = button_capability.Range.UsageMax;

			if (usage_min == 0 || usage_max == 0)
				continue;

			size_t button_index_min = static_cast<size_t>(usage_min - 1);
			size_t button_index_max = static_cast<size_t>(usage_max - 1);

			if (button_capability.UsagePage == button_usage_page && button_index_min < buttons_length_cap)
			{
				button_index_max = std::min(buttons_length_cap - 1, button_index_max);
				button_count = std::max(button_count, button_index_max + 1);
			}
		}

		// should we even allow a joystick that has no buttons?
		if (button_count < 1)
			return false;

		return true;
	}
};

//============================================================
//  mouse_input_rawinput - rawinput mouse module
//============================================================

class mouse_input_rawinput : public rawinput_module
{
public:
	mouse_input_rawinput() : rawinput_module(OSD_MOUSEINPUT_PROVIDER, "rawinput")
	{
	}

protected:
	virtual USHORT usage() override { return HID_USAGE_GENERIC_MOUSE; }

	virtual void add_rawinput_device(RAWINPUTDEVICELIST const &device) override
	{
		// make sure this is a mouse
		if (device.dwType != RIM_TYPEMOUSE)
			return;

		// allocate and link in a new device
		create_rawinput_device<rawinput_mouse_device>(DEVICE_CLASS_MOUSE, device);
	}
};


//============================================================
//  lightgun_input_rawinput - rawinput lightgun module
//============================================================

class lightgun_input_rawinput : public rawinput_module
{
public:
	lightgun_input_rawinput() : rawinput_module(OSD_LIGHTGUNINPUT_PROVIDER, "rawinput")
	{
	}

protected:
	virtual USHORT usage() override { return HID_USAGE_GENERIC_MOUSE; }

	virtual void add_rawinput_device(RAWINPUTDEVICELIST const &device) override
	{
		// make sure this is a mouse
		if (device.dwType != RIM_TYPEMOUSE)
			return;

		// allocate and link in a new device
		create_rawinput_device<rawinput_lightgun_device>(DEVICE_CLASS_LIGHTGUN, device);
	}
};

} // anonymous namespace

} // namespace osd

#else // defined(OSD_WINDOWS)

#include "input_module.h"

namespace osd {

namespace {

MODULE_NOT_SUPPORTED(keyboard_input_rawinput, OSD_KEYBOARDINPUT_PROVIDER, "rawinput")
MODULE_NOT_SUPPORTED(joystick_input_rawinput, OSD_JOYSTICKINPUT_PROVIDER, "rawinput")
MODULE_NOT_SUPPORTED(mouse_input_rawinput, OSD_MOUSEINPUT_PROVIDER, "rawinput")
MODULE_NOT_SUPPORTED(lightgun_input_rawinput, OSD_LIGHTGUNINPUT_PROVIDER, "rawinput")

} // anonymous namespace

} // namespace osd

#endif // defined(OSD_WINDOWS)

MODULE_DEFINITION(KEYBOARDINPUT_RAWINPUT, osd::keyboard_input_rawinput)
MODULE_DEFINITION(JOYSTICKINPUT_RAWINPUT, osd::joystick_input_rawinput)
MODULE_DEFINITION(MOUSEINPUT_RAWINPUT, osd::mouse_input_rawinput)
MODULE_DEFINITION(LIGHTGUNINPUT_RAWINPUT, osd::lightgun_input_rawinput)
