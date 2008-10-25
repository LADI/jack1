/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2007,2008 Nedko Arnaudov
    Copyright (C) 2007-2008 Juuso Alasuutari
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef JACKCONTROLLER_INTERNAL_H__04D54D51_3D79_49A2_A1DA_F8587E9E7F42__INCLUDED
#define JACKCONTROLLER_INTERNAL_H__04D54D51_3D79_49A2_A1DA_F8587E9E7F42__INCLUDED

#include <stdbool.h>
#include "jackctl.h"
#include "jackdbus.h"

struct jack_controller
{
	jackctl_server server;

	void * patchbay_context;

	bool started;

	const char ** driver_names;
	unsigned int drivers_count;

	/* current driver, NULL if not driver is selected */
	jackctl_driver driver;
	bool driver_set;

	struct jack_dbus_object_descriptor dbus_descriptor;
};

#define JACK_CONF_HEADER_TEXT						\
	"JACK settings, as persisted by D-Bus object.\n"		\
	"You probably don't want to edit this because\n"		\
	"it will be overwritten next time jackdbus saves.\n"

jackctl_driver
jack_controller_find_driver(
	jackctl_server server,
	const char * driver_name);

jackctl_parameter
jack_controller_find_parameter(
	const JSList * parameters_list,
	const char * parameter_name);

bool
jack_controller_start_server(
	struct jack_controller * controller_ptr,
	void *dbus_call_context_ptr);

bool
jack_controller_stop_server(
	struct jack_controller * controller_ptr,
	void *dbus_call_context_ptr);

bool
jack_controller_select_driver(
	struct jack_controller * controller_ptr,
	const char * driver_name);

void
jack_controller_settings_set_driver_option(
	jackctl_driver driver,
	const char *option_name,
	const char *option_value);

void
jack_controller_settings_set_engine_option(
	struct jack_controller *controller_ptr,
	const char *option_name,
	const char *option_value);

bool
jack_controller_settings_save_engine_options(
	void *context,
	struct jack_controller *controller_ptr,
	void *dbus_call_context_ptr);

bool
jack_controller_settings_write_option(
	void *context,
	const char *name,
	const char *content,
	void *dbus_call_context_ptr);

bool
jack_controller_settings_save_driver_options(
	void *context,
	jackctl_driver driver,
	void *dbus_call_context_ptr);

bool
jack_controller_patchbay_init(
	struct jack_controller * controller_ptr);

void
jack_controller_patchbay_uninit(
	struct jack_controller * controller_ptr);

void *
jack_controller_patchbay_client_appeared_callback(
	void * server_context,
	uint64_t client_id,
	const char * client_name);

void
jack_controller_patchbay_client_disappeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context);

void *
jack_controller_patchbay_port_appeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	const char * port_name,
	uint32_t port_flags,
	uint32_t port_type);

void
jack_controller_patchbay_port_disappeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	void * port_context);

void *
jack_controller_patchbay_ports_connected_callback(
	void * server_context,
	uint64_t client1_id,
	void * client1_context,
	uint64_t port1_id,
	void * port1_context,
	uint64_t client2_id,
	void * client2_context,
	uint64_t port2_id,
	void * port2_context,
	uint64_t connection_id);

void
jack_controller_patchbay_ports_disconnected_callback(
	void * server_context,
	uint64_t client1_id,
	void * client1_context,
	uint64_t port1_id,
	void * port1_context,
	uint64_t client2_id,
	void * client2_context,
	uint64_t port2_id,
	void * port2_context,
	uint64_t connection_id,
	void * connection_context);

extern struct jack_dbus_interface_descriptor g_jack_controller_iface_introspectable;
extern struct jack_dbus_interface_descriptor g_jack_controller_iface_control;
extern struct jack_dbus_interface_descriptor g_jack_controller_iface_configure;
extern struct jack_dbus_interface_descriptor g_jack_controller_iface_patchbay;
extern struct jack_dbus_interface_descriptor g_jack_controller_iface_transport;

#endif /* #ifndef JACKCONTROLLER_INTERNAL_H__04D54D51_3D79_49A2_A1DA_F8587E9E7F42__INCLUDED */
