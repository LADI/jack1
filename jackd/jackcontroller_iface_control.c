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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <dbus/dbus.h>

#include <jack/internal.h>

#include "jackdbus.h"
#include "jackcontroller_internal.h"

#define controller_ptr ((struct jack_controller *)call->context)

/*
 * Check if the supplied method name exists in org.jackaudio.JackControl,
 * if it does execute it and return true. Otherwise return false.
 */
static
bool
jack_control_run_method(
	struct jack_dbus_method_call * call,
	const struct jack_dbus_interface_method_descriptor * methods)
{
	int type;
	message_arg_t arg;

	/* use empty reply if not overriden in the code that follows */
	type = DBUS_TYPE_INVALID;

	if (strcmp (call->method_name, "Exit") == 0)
	{
                g_exit_command = TRUE;
	}
	else if (strcmp (call->method_name, "IsStarted") == 0)
	{
		type = DBUS_TYPE_BOOLEAN;
		arg.boolean = (dbus_bool_t) (controller_ptr->started ? TRUE : FALSE);
	}
	else if (strcmp (call->method_name, "StartServer") == 0)
	{
		if (!jack_controller_start_server(controller_ptr, call))
		{
			jack_error ("Failed to start server");
		}
	}
	else if (strcmp (call->method_name, "StopServer") == 0)
	{
		if (!jack_controller_stop_server(controller_ptr, call))
		{
			jack_error ("Failed to stop server");
		}
	}
	else if (strcmp (call->method_name, "GetLoad") == 0)
	{
		if (!controller_ptr->started)
		{
			goto not_started;
		}

		type = DBUS_TYPE_DOUBLE;
		arg.doubl = jackctl_server_get_load(controller_ptr->server);
	}
	else if (strcmp (call->method_name, "GetXruns") == 0)
	{
		type = DBUS_TYPE_UINT32;
		arg.uint32 = (uint32_t)jackctl_server_get_xruns(controller_ptr->server);
	}
	else if (strcmp (call->method_name, "GetSampleRate") == 0)
	{
		if (!controller_ptr->started)
		{
			goto not_started;
		}

		type = DBUS_TYPE_UINT32;
		arg.uint32 = (uint32_t)jackctl_server_get_sample_rate(controller_ptr->server);
	}
	else if (strcmp (call->method_name, "GetLatency") == 0)
	{
		if (!controller_ptr->started)
		{
			goto not_started;
		}

		type = DBUS_TYPE_DOUBLE;
		arg.doubl = jackctl_server_get_latency(controller_ptr->server);
	}
	else if (strcmp (call->method_name, "GetBufferSize") == 0)
	{
		if (!controller_ptr->started)
		{
			goto not_started;
		}

		type = DBUS_TYPE_UINT32;
		arg.uint32 = jackctl_server_get_buffer_size(controller_ptr->server);
	}
	else if (strcmp (call->method_name, "SetBufferSize") == 0)
	{
		dbus_uint32_t buffer_size;

		if (!controller_ptr->started)
		{
			goto not_started;
		}

		if (!jack_dbus_get_method_args(call, DBUS_TYPE_UINT32, &buffer_size, DBUS_TYPE_INVALID))
		{
			/* jack_dbus_get_method_args() has set reply for us */
			goto exit;
		}

		if (!jackctl_server_set_buffer_size(controller_ptr->server, buffer_size))
		{
			jack_dbus_error(
				call,
				JACK_DBUS_ERROR_GENERIC,
				"jackctl_server_set_buffer_size() failed.");

			goto exit;
		}
	}
	else if (strcmp (call->method_name, "IsRealtime") == 0)
	{
		type = DBUS_TYPE_BOOLEAN;
		arg.boolean = (dbus_bool_t) (jackctl_server_is_realtime(controller_ptr->server) ? TRUE : FALSE);
	}
	else if (strcmp (call->method_name, "ResetXruns") == 0)
	{
		jackctl_server_reset_xruns(controller_ptr->server);
	}
	else
	{
		return false;
	}

	jack_dbus_construct_method_return_single(call, type, arg);

	return true;

not_started:
	jack_dbus_error (call, JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
	                 "Can't execute this method with stopped JACK server");

exit:
	return true;
}

#undef controller_ptr

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(IsStarted)
	JACK_DBUS_METHOD_ARGUMENT("started", "b", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(StartServer)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(StopServer)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetLoad)
	JACK_DBUS_METHOD_ARGUMENT("load", "d", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetXruns)
	JACK_DBUS_METHOD_ARGUMENT("xruns_count", "u", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetSampleRate)
	JACK_DBUS_METHOD_ARGUMENT("sample_rate", "u", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetLatency)
	JACK_DBUS_METHOD_ARGUMENT("latency_ms", "d", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetBufferSize)
	JACK_DBUS_METHOD_ARGUMENT("buffer_size_frames", "u", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(SetBufferSize)
	JACK_DBUS_METHOD_ARGUMENT("buffer_size_frames", "u", false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(IsRealtime)
	JACK_DBUS_METHOD_ARGUMENT("realtime", "b", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(ResetXruns)
JACK_DBUS_METHOD_ARGUMENTS_END

static
const struct jack_dbus_interface_method_descriptor g_jack_controller_control_iface_methods[] =
{
	JACK_DBUS_METHOD_DESCRIBE(IsStarted, NULL)
	JACK_DBUS_METHOD_DESCRIBE(StartServer, NULL)
	JACK_DBUS_METHOD_DESCRIBE(StopServer, NULL)
	JACK_DBUS_METHOD_DESCRIBE(GetLoad, NULL)
	JACK_DBUS_METHOD_DESCRIBE(GetXruns, NULL)
	JACK_DBUS_METHOD_DESCRIBE(GetSampleRate, NULL)
	JACK_DBUS_METHOD_DESCRIBE(GetLatency, NULL)
	JACK_DBUS_METHOD_DESCRIBE(GetBufferSize, NULL)
	JACK_DBUS_METHOD_DESCRIBE(SetBufferSize, NULL)
	JACK_DBUS_METHOD_DESCRIBE(IsRealtime, NULL)
	JACK_DBUS_METHOD_DESCRIBE(ResetXruns, NULL)
	JACK_DBUS_METHOD_DESCRIBE_END
};

struct jack_dbus_interface_descriptor g_jack_controller_iface_control =
{
	"org.jackaudio.JackControl",
	jack_control_run_method,
	g_jack_controller_control_iface_methods
};
