// license:BSD-3-Clause
// copyright-holders:Brad Hughes, Antonio Giner, Sergi Clara
//============================================================
//
//  input_mister.cpp - Default unimplemented input modules
//
//============================================================

#include "assignmenthelper.h"
#include "input_wincommon.h"
#include "modules/lib/osdlib.h"
#include "modules/lib/osdobj_common.h"


// emu
#include "emu.h"

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
#define MISTER_MAX_BUTTONS 9

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

#pragma pack(1)

typedef struct nogpu_inputs
{
	uint32_t frame;
	uint8_t  order;
	uint16_t joy1;
	uint16_t joy2;
} nogpu_inputs;

#pragma pack(0)

namespace osd {

namespace {


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
	void update(uint16_t state);

protected:
	u8 hat[4];
	u8 buttons[MAX_BUTTONS];
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
		hat[i] = 0;

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
				&hat[i]);
	}

	add_directional_assignments(assignments, ITEM_ID_INVALID, ITEM_ID_INVALID, hatactual[2], hatactual[3], hatactual[0], hatactual[1]);

	// loop over all buttons
	for (int button = 0; button < MISTER_MAX_BUTTONS; button++)
	{
		input_item_id itemid;
		buttons[button] = 0;

		itemid = input_item_id(ITEM_ID_BUTTON1 + button);

		input_item_id const actual = device.add_item(
				default_button_name(button),
				std::string_view(),
				itemid,
				generic_button_get_state<u8>,
				&buttons[button]);

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

void mister_joystick_device::update(uint16_t state)
{
	hat[0] = (state & MISTER_JOY_UP)    ? 0xff : 0x00;
	hat[1] = (state & MISTER_JOY_DOWN)  ? 0xff : 0x00;
	hat[2] = (state & MISTER_JOY_LEFT)  ? 0xff : 0x00;
	hat[3] = (state & MISTER_JOY_RIGHT) ? 0xff : 0x00;
	buttons[0] = (state & MISTER_JOY_B1)? 0xff : 0x00;
	buttons[1] = (state & MISTER_JOY_B2)? 0xff : 0x00;
	buttons[2] = (state & MISTER_JOY_B3)? 0xff : 0x00;
	buttons[3] = (state & MISTER_JOY_B4)? 0xff : 0x00;
	buttons[4] = (state & MISTER_JOY_B5)? 0xff : 0x00;
	buttons[5] = (state & MISTER_JOY_B6)? 0xff : 0x00;
	buttons[6] = (state & MISTER_JOY_B7)? 0xff : 0x00;
	buttons[7] = (state & MISTER_JOY_B8)? 0xff : 0x00;
	buttons[8] = (state & MISTER_JOY_B9)? 0xff : 0x00;
}

//============================================================
//  mister_joystick_device::reset
//============================================================

void mister_joystick_device::reset()
{
	std::fill(std::begin(buttons), std::end(buttons), 0);
	std::fill(std::begin(hat), std::end(hat), 0);
}


//============================================================
//  joystick_input_mister
//============================================================

class joystick_input_mister : public osd_module, public input_module
{
public:
	joystick_input_mister() :
		osd_module(OSD_JOYSTICKINPUT_PROVIDER, "mister"),
		joystick1("MiSTer", "joy1", *this, 1),
		joystick2("MiSTer", "joy2", *this, 2) {};

	virtual int init(osd_interface &osd, const osd_options &options) override;
	virtual void input_init(running_machine &machine) override;
	virtual void poll_if_necessary(bool relative_reset) override;

private:
	int m_sockfd = -1; //INVALID_SOCKET;
	sockaddr_in m_server_addr;
	nogpu_inputs inputs {0};

	mister_joystick_device joystick1;
	mister_joystick_device joystick2;
};

//============================================================
//  joystick_input_mister::init
//============================================================

int joystick_input_mister::init(osd_interface &osd, const osd_options &options)
{
	int result;

	#ifdef _WIN32
		osd_printf_verbose("nogpu: Initializing Winsock...");
		WSADATA wsa;
		result = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (result != NO_ERROR)
		{
			osd_printf_verbose("Failed. Error code : %d", WSAGetLastError());
			return -1;
		}
		osd_printf_verbose("done.\n");
	#endif

	osd_printf_verbose("nogpu: Initializing socket... ");
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

	osd_printf_verbose("nogpu: Setting socket async...\n");

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

	// Signal server for input
	char buffer[1];
	sendto(m_sockfd, buffer, 1, 0, (sockaddr *)&m_server_addr, sizeof(m_server_addr));

	return 0;
}

//============================================================
//  joystick_input_mister::input_init
//============================================================

void joystick_input_mister::input_init(running_machine &machine)
{
	osd::input_device &osddev1 = machine.input().add_device(DEVICE_CLASS_JOYSTICK, "MiSTer", "joy1", (void *)&joystick1);
	joystick1.configure(osddev1);

	osd::input_device &osddev2 = machine.input().add_device(DEVICE_CLASS_JOYSTICK, "MiSTer", "joy2", (void *)&joystick2);
	joystick2.configure(osddev2);
}

//============================================================
//  joystick_input_mister::poll_if_necessary
//============================================================

void joystick_input_mister::poll_if_necessary(bool relative_reset)
{
	nogpu_inputs new_inputs = inputs;
	socklen_t server_addr_size = sizeof(m_server_addr);
	int bytes_recv = 0;
	bool must_update = false;

	// Poll MiSTer server
	do
	{
		bytes_recv = recvfrom(m_sockfd, (char *)&new_inputs, sizeof(nogpu_inputs), 0, (sockaddr*)&m_server_addr, &server_addr_size);
		if (bytes_recv == sizeof(nogpu_inputs))
		{
			if (new_inputs.frame > inputs.frame || (new_inputs.frame == inputs.frame && new_inputs.order > inputs.order))
			{
				inputs = new_inputs;
				must_update = true;
			}
		}
	}
	while (bytes_recv > 0);

	// Update joysticks button state
	if (must_update)
	{
		joystick1.update(inputs.joy1);
		joystick2.update(inputs.joy2);
	}
}


} // anonymous namesapce

} // namespace osd


MODULE_DEFINITION(JOYSTICKINPUT_MISTER, osd::joystick_input_mister)
