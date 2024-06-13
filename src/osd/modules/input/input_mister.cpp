// license:BSD-3-Clause
// copyright-holders:Antonio Giner, Sergi Clara
//============================================================
//
//  input_mister.cpp - nogpu input module
//
//============================================================

#include "assignmenthelper.h"
#include "modules/lib/osdlib.h"
#include "modules/lib/osdobj_common.h"
#include "input_common.h"


// emu
#include "emu.h"
#include "strconv.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#define socklen_t int
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// nogpu UDP input server
#define UDP_INPUT_PORT 32101
#define MISTER_MAX_BUTTONS 10

//joystick map
#define MISTER_JOY_RIGHT (1 << 0)
#define MISTER_JOY_LEFT  (1 << 1)
#define MISTER_JOY_DOWN  (1 << 2)
#define MISTER_JOY_UP    (1 << 3)
#define MISTER_JOY_B1    (1 << 4)
#define MISTER_JOY_B2    (1 << 5)
#define MISTER_JOY_B3    (1 << 6)
#define MISTER_JOY_B4    (1 << 7)
#define MISTER_JOY_B5    (1 << 8)
#define MISTER_JOY_B6    (1 << 9)
#define MISTER_JOY_B7    (1 << 10)
#define MISTER_JOY_B8    (1 << 11)
#define MISTER_JOY_B9    (1 << 12)
#define MISTER_JOY_B10   (1 << 13)

//devices
#define MISTER_JOYSTICK_DEVICE (1 << 0)
#define MISTER_KEYBOARD_DEVICE (1 << 1)
#define MISTER_MOUSE_DEVICE    (1 << 2)

#pragma pack(1)

typedef struct nogpu_joy_dig_inputs
{
	uint32_t frame;
	uint8_t  order;
	uint16_t joy1;
	uint16_t joy2;
} nogpu_joy_dig_inputs;

typedef struct nogpu_joy_inputs
{
	uint32_t frame;
	uint8_t  order;
	uint16_t joy1;
	uint16_t joy2;
	int8_t   joy1_LX_analog;
	int8_t   joy1_LY_analog;
	int8_t   joy1_RX_analog;
	int8_t   joy1_RY_analog;
	int8_t   joy2_LX_analog;
	int8_t   joy2_LY_analog;
	int8_t   joy2_RX_analog;
	int8_t   joy2_RY_analog;
} nogpu_joy_inputs;

typedef struct nogpu_keyboard_inputs
{
	uint32_t frame;
	uint8_t  order;
	uint8_t  keys[32];
} nogpu_keyboard_inputs;

typedef struct nogpu_ps2_inputs
{
	uint32_t frame;
	uint8_t  order;
	uint8_t  keys[32];
	uint8_t  mouse_bits;
	uint8_t  mouse_X;
	uint8_t  mouse_Y;
	uint8_t  mouse_Z;
} nogpu_ps2_inputs;

#pragma pack(0)

namespace osd {

namespace {


//============================================================
//  mister_init
//============================================================

class mister_input
{
public:
	static int init_socket(int device, const osd_options &options);
	static void deinit_socket();
	static void poll_server();

protected:
	inline static int m_initialized;
	inline static int m_sockfd;
	inline static sockaddr_in m_server_addr;

	inline static nogpu_joy_inputs m_joy_inputs;
	inline static nogpu_ps2_inputs m_ps2_inputs;

	inline static char recv_buff[std::max(sizeof(nogpu_joy_inputs), sizeof(nogpu_ps2_inputs))];
	inline static bool joy_update_pending;
	inline static bool keyboard_update_pending;
	inline static bool mouse_update_pending;
};


//============================================================
//  mister_input::init_socket
//============================================================

int mister_input::init_socket(int device, const osd_options &options)
{
	m_joy_inputs = {0};
	m_ps2_inputs = {0};

	if (m_initialized != 0)
	{
		if (m_initialized & device)
			deinit_socket();
		else
		{
			// already initialized, skip initialization
			m_initialized |= device;
			return 0;
		}
	}

	#ifdef _WIN32
		osd_printf_verbose("nogpu_input: Initializing Winsock...");
		WSADATA wsa;
		int result = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (result != NO_ERROR)
		{
			osd_printf_verbose("Failed. Error code : %d", WSAGetLastError());
			return -1;
		}
		osd_printf_verbose("done.\n");
	#endif

	osd_printf_verbose("nogpu_input: Initializing socket... ");
	m_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (m_sockfd < 0)
	{
		osd_printf_verbose("Could not create socket\n");
		return -1;
	}
	else
		osd_printf_verbose(" done.\n");

	short port = UDP_INPUT_PORT;
	const char* local_host = options.mister_ip();

	m_server_addr = {};
	m_server_addr.sin_family = AF_INET;
	m_server_addr.sin_port = htons(port);
	m_server_addr.sin_addr.s_addr = inet_addr(local_host);

	osd_printf_verbose("nogpu_input: Setting socket async... ");

	#ifdef _WIN32
		u_long opt = 1;
		if (ioctlsocket(m_sockfd, FIONBIO, &opt) < 0)
			osd_printf_verbose("Could not set nonblocking.\n");
	#else
		int flags;
		flags = fcntl(m_sockfd, F_GETFD, 0);
		if (flags < 0)
			osd_printf_verbose("Could not get socket flags.\n");
		else
		{
			flags |= O_NONBLOCK;
			if (fcntl(m_sockfd, F_SETFL, flags) < 0)
				osd_printf_verbose("Could not set nonblocking.\n");
		}
	#endif
	osd_printf_verbose(" done.\n");

	// Signal server for input
	char buffer[1];
	sendto(m_sockfd, buffer, 1, 0, (sockaddr *)&m_server_addr, sizeof(m_server_addr));

	m_initialized |= device;
	return 0;
}


//============================================================
//  mister_input::deinit_socket
//============================================================

void mister_input::deinit_socket()
{
	osd_printf_verbose("nogpu_input: closing input socket.\n");
#ifdef WIN32
	closesocket(m_sockfd);
	WSACleanup();
#else
	close(m_sockfd);
#endif
	m_initialized = 0;
}


//============================================================
//  mister_input::poll_server
//============================================================

// Note: We have no way to tell whether the joysticks are analogue or digital during initialization.
// With regards to PS2 inputs, we neither know if we have both keyboard and mouse or only one of them.
// It's all determined later here by the size of the packes we receive.

void mister_input::poll_server()
{
	socklen_t server_addr_size = sizeof(m_server_addr);
	int bytes_recv = 0;
	memset(recv_buff, 0, sizeof(recv_buff));

	// Poll MiSTer server
	do
	{
		bytes_recv = recvfrom(m_sockfd, recv_buff, sizeof(recv_buff), 0, (sockaddr*)&m_server_addr, &server_addr_size);
		if (bytes_recv < 0)
			continue;

		switch (bytes_recv)
		{
			case sizeof(nogpu_joy_dig_inputs):
			case sizeof(nogpu_joy_inputs):
			{
				nogpu_joy_inputs &new_inputs = (nogpu_joy_inputs&) recv_buff;
				if (new_inputs.frame > m_joy_inputs.frame || (new_inputs.frame == m_joy_inputs.frame && new_inputs.order > m_joy_inputs.order))
				{
					m_joy_inputs = new_inputs;
					joy_update_pending = true;
				}
				break;
			}
			case sizeof(nogpu_keyboard_inputs):
			{
				nogpu_keyboard_inputs &new_inputs = (nogpu_keyboard_inputs&) recv_buff;
				if (new_inputs.frame > m_ps2_inputs.frame || (new_inputs.frame == m_ps2_inputs.frame && new_inputs.order > m_ps2_inputs.order))
				{
					nogpu_keyboard_inputs &inputs = (nogpu_keyboard_inputs&) m_ps2_inputs;
					inputs = new_inputs;
					keyboard_update_pending = true;
				}
				break;
			}
			case sizeof(nogpu_ps2_inputs):
			{
				nogpu_ps2_inputs &new_inputs = (nogpu_ps2_inputs&) recv_buff;
				if (new_inputs.frame > m_ps2_inputs.frame || (new_inputs.frame == m_ps2_inputs.frame && new_inputs.order > m_ps2_inputs.order))
				{
					m_ps2_inputs = new_inputs;
					keyboard_update_pending = true;
					mouse_update_pending = true;
				}
				break;
			}
		}
	}
	while (bytes_recv > 0);
}

//============================================================
//  mister_joystick_device
//============================================================

class mister_joystick_device : public device_info, protected joystick_assignment_helper
{
public:
	mister_joystick_device(
			std::string &&name,
			std::string &&id,
			input_module &module,
			u32 player) : device_info(std::move(name), std::move(id), module) {};

	virtual void poll(bool relative_reset) override {};
	virtual void reset() override;
	virtual void configure(osd::input_device &device) override;
	void update(uint16_t state, int8_t lx, int8_t ly, int8_t rx, int8_t ry);

protected:
	u8 m_hats[4];
	u8 m_buttons[MAX_BUTTONS];
	s32 m_axes[4];
};

//============================================================
//  mister_joystick_device::configure
//============================================================

void mister_joystick_device::configure(osd::input_device &device)
 {
	input_device::assignment_vector assignments;
	char tempname[32];

	input_item_id hatactual[4];

	for (int i = 0 ; i < 4; i++)
	{
		input_item_id itemid;
		m_hats[i] = 0;

		switch (i)
		{
			case 0:
				snprintf(tempname, sizeof(tempname), "Hat Up");
				break;
			case 1:
				snprintf(tempname, sizeof(tempname), "Hat Down");
				break;
			case 2:
				snprintf(tempname, sizeof(tempname), "Hat Left");
				break;
			case 3:
				snprintf(tempname, sizeof(tempname), "Hat Right");
				break;
		}

		itemid = input_item_id(ITEM_ID_HAT1UP + i);

		hatactual[i] = device.add_item(
				tempname,
				std::string_view(),
				itemid,
				generic_button_get_state<u8>,
				&m_hats[i]);
	}

	// add axes
	input_item_id axisactual[4];
	for (int i = 0; i < 4; i++)
	{
		char tempname[3];
		snprintf(tempname, sizeof(tempname), "A%d", i);
		axisactual[i] = device.add_item(tempname, std::string_view(), input_item_id(ITEM_ID_XAXIS + i), generic_axis_get_state<s32>, &m_axes[i]);
	}

	add_directional_assignments(assignments, axisactual[0], axisactual[1], hatactual[2], hatactual[3], hatactual[0], hatactual[1]);

	// loop over all buttons
	for (int button = 0; button < MISTER_MAX_BUTTONS; button++)
	{
		input_item_id itemid;
		m_buttons[button] = 0;

		itemid = input_item_id(ITEM_ID_BUTTON1 + button);

		input_item_id const actual = device.add_item(
				default_button_name(button),
				std::string_view(),
				itemid,
				generic_button_get_state<u8>,
				&m_buttons[button]);

		input_seq const seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, actual));
		assignments.emplace_back(ioport_type(IPT_BUTTON1 + button), SEQ_TYPE_STANDARD, seq);

		// assign the first few buttons to UI actions and pedals
		switch (button)
		{
		case 0:
			assignments.emplace_back(IPT_PEDAL, SEQ_TYPE_INCREMENT, seq);
			assignments.emplace_back(IPT_UI_SELECT, SEQ_TYPE_STANDARD, seq);
			break;
		case 1:
			assignments.emplace_back(IPT_PEDAL2, SEQ_TYPE_INCREMENT, seq);
			assignments.emplace_back(IPT_UI_BACK, SEQ_TYPE_STANDARD, seq);
			break;
		case 2:
			assignments.emplace_back(IPT_PEDAL3, SEQ_TYPE_INCREMENT, seq);
			assignments.emplace_back(IPT_UI_CLEAR, SEQ_TYPE_STANDARD, seq);
			break;
		case 3:
			assignments.emplace_back(IPT_UI_HELP, SEQ_TYPE_STANDARD, seq);
			break;
		}
	}

	// set default assignments
	device.set_default_assignments(std::move(assignments));
 }

//============================================================
//  mister_joystick_device::update
//============================================================

void mister_joystick_device::update(uint16_t state, int8_t lx, int8_t ly, int8_t rx, int8_t ry)
{
	m_hats[0] = (state & MISTER_JOY_UP)    ? 0xff : 0x00;
	m_hats[1] = (state & MISTER_JOY_DOWN)  ? 0xff : 0x00;
	m_hats[2] = (state & MISTER_JOY_LEFT)  ? 0xff : 0x00;
	m_hats[3] = (state & MISTER_JOY_RIGHT) ? 0xff : 0x00;
	m_buttons[0] = (state & MISTER_JOY_B1)? 0xff : 0x00;
	m_buttons[1] = (state & MISTER_JOY_B2)? 0xff : 0x00;
	m_buttons[2] = (state & MISTER_JOY_B3)? 0xff : 0x00;
	m_buttons[3] = (state & MISTER_JOY_B4)? 0xff : 0x00;
	m_buttons[4] = (state & MISTER_JOY_B5)? 0xff : 0x00;
	m_buttons[5] = (state & MISTER_JOY_B6)? 0xff : 0x00;
	m_buttons[6] = (state & MISTER_JOY_B7)? 0xff : 0x00;
	m_buttons[7] = (state & MISTER_JOY_B8)? 0xff : 0x00;
	m_buttons[8] = (state & MISTER_JOY_B9)? 0xff : 0x00;
	m_buttons[9] = (state & MISTER_JOY_B10)? 0xff : 0x00;
	m_axes[0] = lx * 512;
	m_axes[1] = ly * 512;
	m_axes[2] = rx * 512;
	m_axes[3] = ry * 512;
}

//============================================================
//  mister_joystick_device::reset
//============================================================

void mister_joystick_device::reset()
{
	std::fill(std::begin(m_buttons), std::end(m_buttons), 0);
	std::fill(std::begin(m_hats), std::end(m_hats), 0);
	std::fill(std::begin(m_axes), std::end(m_axes), 0);
}


//============================================================
//  joystick_input_mister
//============================================================

class joystick_input_mister : public osd_module, public input_module, public mister_input
{
public:
	joystick_input_mister() :
		osd_module(OSD_JOYSTICKINPUT_PROVIDER, "mister"),
		joystick1("MiSTer", "joy1", *this, 1),
		joystick2("MiSTer", "joy2", *this, 2) {};

	~joystick_input_mister() { deinit_socket(); };
	virtual int init(osd_interface &osd, const osd_options &options) override { return init_socket(MISTER_JOYSTICK_DEVICE, options); };
	virtual void input_init(running_machine &machine) override;
	virtual void poll_if_necessary(bool relative_reset) override;

private:
	mister_joystick_device joystick1;
	mister_joystick_device joystick2;
};

//============================================================
//  joystick_input_mister::input_init
//============================================================

void joystick_input_mister::input_init(running_machine &machine)
{
	osd::input_device &osddev1 = machine.input().add_device(DEVICE_CLASS_JOYSTICK, "MiSTer", "joy1", (void *)&joystick1);
	joystick1.configure(osddev1);
	joystick1.reset();

	osd::input_device &osddev2 = machine.input().add_device(DEVICE_CLASS_JOYSTICK, "MiSTer", "joy2", (void *)&joystick2);
	joystick2.configure(osddev2);
	joystick2.reset();
}

//============================================================
//  joystick_input_mister::poll_if_necessary
//============================================================

void joystick_input_mister::poll_if_necessary(bool relative_reset)
{
	poll_server();

	// Update joysticks button state
	if (joy_update_pending)
	{
		joystick1.update(m_joy_inputs.joy1, m_joy_inputs.joy1_LX_analog, m_joy_inputs.joy1_LY_analog, m_joy_inputs.joy1_RX_analog, m_joy_inputs.joy1_RY_analog);
		joystick2.update(m_joy_inputs.joy2, m_joy_inputs.joy2_LX_analog, m_joy_inputs.joy2_LY_analog, m_joy_inputs.joy2_RX_analog, m_joy_inputs.joy2_RY_analog);
		joy_update_pending = false;
	}
}


// KEYBOARD

//============================================================
//  mister_keyboard_device
//============================================================

class mister_keyboard_device : public device_info
{
public:
	mister_keyboard_device(
			std::string &&name,
			std::string &&id,
			input_module &module,
			u32 player) : device_info(std::move(name), std::move(id), module) {};

	virtual void poll(bool relative_reset) override {};
	virtual void reset() override;
	virtual void configure(osd::input_device &device) override;
	void update(uint8_t *keys);

protected:
	uint8_t m_state[256];
};


//============================================================
//  mister_keyboard_device::configure
//============================================================

void mister_keyboard_device::configure(osd::input_device &device)
{
	keyboard_trans_table const &table = keyboard_trans_table::instance();

	// populate it
	for (int keynum = 0; table[keynum].mame_key != ITEM_ID_INVALID; keynum++)
	{
		input_item_id itemid = table[keynum].mame_key;
		device.add_item(
				table[keynum].ui_name,
				std::string_view(),
				itemid,
				generic_button_get_state<std::uint8_t>,
				&m_state[table[keynum].sdl_scancode]);
	}
}

//============================================================
//  mister_keyboard_device::update
//============================================================

void mister_keyboard_device::update(uint8_t *keys)
{
	for (int i = 0; i < 256; i++)
		m_state[i] = (1 & (keys[i / 8] >> (i % 8))) ? 0x80 : 0x00;
}

//============================================================
//  mister_keyboard_device::reset
//============================================================

void mister_keyboard_device::reset()
{
	std::fill(std::begin(m_state), std::end(m_state), 0);
}

//============================================================
//  keyboard_input_mister
//============================================================

class keyboard_input_mister : public osd_module, public input_module, public mister_input
{
public:
	keyboard_input_mister() :
		osd_module(OSD_KEYBOARDINPUT_PROVIDER, "mister"),
		keyboard1("MiSTer", "keyboard", *this, 1) {};

	~keyboard_input_mister() { deinit_socket(); };
	virtual int init(osd_interface &osd, const osd_options &options) override { return init_socket(MISTER_KEYBOARD_DEVICE, options); };
	virtual void input_init(running_machine &machine) override;
	virtual void poll_if_necessary(bool relative_reset) override;

private:
	mister_keyboard_device keyboard1;
};

//============================================================
//  keyboard_input_mister::input_init
//============================================================

void keyboard_input_mister::input_init(running_machine &machine)
{
	osd::input_device &osddev1 = machine.input().add_device(DEVICE_CLASS_KEYBOARD, "MiSTer", "keyboard1", (void *)&keyboard1);
	keyboard1.configure(osddev1);
	keyboard1.reset();
}

//============================================================
//  keyboard_input_mister::poll_if_necessary
//============================================================

void keyboard_input_mister::poll_if_necessary(bool relative_reset)
{
	poll_server();

	// Update keyboard state
	if (keyboard_update_pending)
	{
		keyboard1.update(m_ps2_inputs.keys);
		keyboard_update_pending = false;
	}
}


// MOUSE

//============================================================
//  mister_mouse_device
//============================================================

class mister_mouse_device : public device_info
{
public:
	mister_mouse_device(
			std::string &&name,
			std::string &&id,
			input_module &module,
			u32 player) : device_info(std::move(name), std::move(id), module) {};

	virtual void poll(bool relative_reset) override;
	virtual void reset() override;
	virtual void configure(osd::input_device &device) override;
	void update(uint8_t mouse_bits, uint8_t mouse_X, uint8_t mouse_Y, uint8_t mouse_Z);


protected:
	s32 m_lX, m_lY;
	s32 m_x, m_y;
	uint8_t m_buttons[3];
};

//============================================================
//  mister_mouse_device::configure
//============================================================

void mister_mouse_device::configure(osd::input_device &device)
{
	// add the m_axes
	device.add_item(
			"X",
			std::string_view(),
			ITEM_ID_XAXIS,
			generic_axis_get_state<s32>,
			&m_lX);
	device.add_item(
			"Y",
			std::string_view(),
			ITEM_ID_YAXIS,
			generic_axis_get_state<s32>,
			&m_lY);

	// populate the buttons
	for (int butnum = 0; butnum < 3; butnum++)
	{
		device.add_item(
				default_button_name(butnum),
				std::string_view(),
				input_item_id(ITEM_ID_BUTTON1 + butnum),
				generic_button_get_state<std::uint8_t>,
				&m_buttons[butnum]);
	}
}

//============================================================
//  mister_mouse_device::poll
//============================================================

void mister_mouse_device::poll(bool relative_reset)
{
	if (relative_reset)
	{
		m_lX = std::exchange(m_x, 0);
		m_lY = std::exchange(m_y, 0);
	}
}

//============================================================
//  mister_mouse_device::update
//============================================================

void mister_mouse_device::update(uint8_t mouse_bits, uint8_t mouse_X, uint8_t mouse_Y, uint8_t mouse_Z)
{
	s32 rel_x = mouse_X - ((mouse_bits << 4) & 0x100);
	s32 rel_y = mouse_Y - ((mouse_bits << 3) & 0x100);

	m_x += rel_x * input_device::RELATIVE_PER_PIXEL;
	m_y -= rel_y * input_device::RELATIVE_PER_PIXEL;

	for (int i = 0; i < 3; i++)
		m_buttons[i] = mouse_bits & (1 << i) ? 0x80 : 0x00;
}

//============================================================
//  mister_mouse_device::reset
//============================================================

void mister_mouse_device::reset()
{
	std::fill(std::begin(m_buttons), std::end(m_buttons), 0);
	m_lX = m_lY = 0;
	m_x = m_y = 0;
}

//============================================================
//  mouse_input_mister
//============================================================

class mouse_input_mister : public osd_module, public input_module, public mister_input
{
public:
	mouse_input_mister() :
		osd_module(OSD_MOUSEINPUT_PROVIDER, "mister"),
		mouse1("MiSTer", "mouse", *this, 1) {};

	~mouse_input_mister() { deinit_socket(); };
	virtual int init(osd_interface &osd, const osd_options &options) override { return init_socket(MISTER_MOUSE_DEVICE, options); };
	virtual void input_init(running_machine &machine) override;
	virtual void poll_if_necessary(bool relative_reset) override;

private:
	mister_mouse_device mouse1;
};

//============================================================
//  mouse_input_mister::input_init
//============================================================

void mouse_input_mister::input_init(running_machine &machine)
{
	osd::input_device &osddev1 = machine.input().add_device(DEVICE_CLASS_MOUSE, "MiSTer", "mouse1", (void *)&mouse1);
	mouse1.configure(osddev1);
	mouse1.reset();
}

//============================================================
//  mouse_input_mister::poll_if_necessary
//============================================================

void mouse_input_mister::poll_if_necessary(bool relative_reset)
{
	poll_server();

	// Update keyboard state
	if (mouse_update_pending)
	{
		mouse1.update(m_ps2_inputs.mouse_bits, m_ps2_inputs.mouse_X, m_ps2_inputs.mouse_Y, m_ps2_inputs.mouse_Z);
		mouse_update_pending = false;
	}

	mouse1.poll(relative_reset);
}


} // anonymous namesapce

} // namespace osd


MODULE_DEFINITION(JOYSTICKINPUT_MISTER, osd::joystick_input_mister)
MODULE_DEFINITION(KEYBOARDINPUT_MISTER, osd::keyboard_input_mister)
MODULE_DEFINITION(MOUSEINPUT_MISTER, osd::mouse_input_mister)
