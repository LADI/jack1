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

#include <config.h>

#include <string.h>
#include <dbus/dbus.h>

#include <jack/internal.h>

#include "jackcontroller.h"
#include "jackcontroller_internal.h"
#include "jackcontroller_xml.h"

struct jack_dbus_interface_descriptor * g_jackcontroller_interfaces[] =
{
	&g_jack_controller_iface_introspectable,
	&g_jack_controller_iface_control,
	&g_jack_controller_iface_configure,
	&g_jack_controller_iface_patchbay,
	&g_jack_controller_iface_transport,
	NULL
};

jackctl_driver
jack_controller_find_driver(
	jackctl_server server,
	const char * driver_name)
{
	const JSList * node_ptr;

	node_ptr = jackctl_server_get_drivers_list(server);

	while (node_ptr)
	{
		if (strcmp(jackctl_driver_get_name((jackctl_driver)node_ptr->data), driver_name) == 0)
		{
			return (jackctl_driver)node_ptr->data;
		}

		node_ptr = jack_slist_next(node_ptr);
	}

	return NULL;
}

jackctl_parameter
jack_controller_find_parameter(
	const JSList * parameters_list,
	const char * parameter_name)
{
	while (parameters_list)
	{
		if (strcmp(jackctl_parameter_get_name((jackctl_parameter)parameters_list->data), parameter_name) == 0)
		{
			return (jackctl_parameter)parameters_list->data;
		}

		parameters_list = jack_slist_next(parameters_list);
	}

	return NULL;
}

bool
jack_controller_select_driver(
	struct jack_controller * controller_ptr,
	const char * driver_name)
{
	jackctl_driver driver;

	driver = jack_controller_find_driver(controller_ptr->server, driver_name);
	if (driver == NULL)
	{
		return false;
	}

	jack_info("driver \"%s\" selected", driver_name);

	controller_ptr->driver = driver;
	controller_ptr->driver_set = true;

	return true;
}

bool
jack_controller_start_server(
	struct jack_controller * controller_ptr,
	void *dbus_call_context_ptr)
{
	jack_info("Starting jack server...");

	if (controller_ptr->driver == NULL)
	{
		jack_dbus_error(dbus_call_context_ptr, JACK_DBUS_ERROR_GENERIC, "Select driver first!");
		return FALSE;
	}

	if (!jackctl_server_start(
		    controller_ptr->server,
		    controller_ptr->driver,
		    controller_ptr,
		    jack_controller_patchbay_client_appeared_callback,
		    jack_controller_patchbay_client_disappeared_callback,
		    jack_controller_patchbay_port_appeared_callback,
		    jack_controller_patchbay_port_disappeared_callback,
		    jack_controller_patchbay_ports_connected_callback,
		    jack_controller_patchbay_ports_disconnected_callback))
	{
		return FALSE;
	}

	controller_ptr->started = true;

	return TRUE;
}

bool
jack_controller_stop_server(
	struct jack_controller * controller_ptr,
	void *dbus_call_context_ptr)
{
	jack_info("Stopping jack server...");

	if (!jackctl_server_stop(controller_ptr->server))
	{
		return FALSE;
	}

	controller_ptr->started = false;

	return TRUE;
}

void *
jack_controller_create(
        DBusConnection *connection)
{
	struct jack_controller *controller_ptr;
	const JSList * node_ptr;
	const char ** driver_name_target;
	JSList * drivers;
	DBusObjectPathVTable vtable =
	{
		jack_dbus_message_handler_unregister,
		jack_dbus_message_handler,
		NULL
	};

	controller_ptr = malloc(sizeof(struct jack_controller));
	if (!controller_ptr)
	{
		jack_error("Ran out of memory trying to allocate struct jack_controller");
		goto fail;
	}

	if (!jack_controller_patchbay_init(controller_ptr))
	{
		jack_error("Failed to initialize patchbay district");
		goto fail_free;
	}

	controller_ptr->server = jackctl_server_create(NULL);
	if (controller_ptr->server == NULL)
	{
		jack_error("Failed to create server object");
		goto fail_uninit_patchbay;
	}

	controller_ptr->started = false;
	controller_ptr->driver = NULL;
	controller_ptr->driver_set = true;

	drivers = (JSList *)jackctl_server_get_drivers_list(controller_ptr->server);
	controller_ptr->drivers_count = jack_slist_length(drivers);
	controller_ptr->driver_names = malloc(controller_ptr->drivers_count * sizeof(const char *));
	if (controller_ptr->driver_names == NULL)
	{
		jack_error("Ran out of memory trying to allocate driver names array");
		goto fail_destroy_server;
	}

	driver_name_target = controller_ptr->driver_names;
	node_ptr = jackctl_server_get_drivers_list(controller_ptr->server);
	while (node_ptr != NULL)
	{
		*driver_name_target = jackctl_driver_get_name((jackctl_driver)node_ptr->data);

		/* select default driver */
		if (controller_ptr->driver == NULL && strcmp(*driver_name_target, JACK_DEFAULT_DRIVER) == 0)
		{
			controller_ptr->driver = (jackctl_driver_t *)node_ptr->data;
		}

		node_ptr = jack_slist_next(node_ptr);
		driver_name_target++;
	}

	controller_ptr->dbus_descriptor.context = controller_ptr;
	controller_ptr->dbus_descriptor.interfaces = g_jackcontroller_interfaces;

	if (!dbus_connection_register_object_path(
		    connection,
		    JACK_CONTROLLER_OBJECT_PATH,
		    &vtable,
		    &controller_ptr->dbus_descriptor))
	{
		jack_error("Ran out of memory trying to register D-Bus object path");
		goto fail_free_driver_names_array;
	}

	jack_controller_settings_load(controller_ptr);

	return controller_ptr;

fail_free_driver_names_array:
	free(controller_ptr->driver_names);

fail_destroy_server:
	jackctl_server_destroy(controller_ptr->server);

fail_uninit_patchbay:
	jack_controller_patchbay_uninit(controller_ptr);

fail_free:
	free(controller_ptr);

fail:
	return NULL;
}

#define controller_ptr ((struct jack_controller *)context)

void
jack_controller_destroy(
        void * context)
{
	if (controller_ptr->started)
	{
		jack_controller_stop_server(controller_ptr, NULL);
	}

	free(controller_ptr->driver_names);

	jackctl_server_destroy(controller_ptr->server);

	jack_controller_patchbay_uninit(controller_ptr);

	free(controller_ptr);
}
