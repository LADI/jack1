/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2008 Nedko Arnaudov

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

#include "jackdbus.h"

static
const struct jack_dbus_interface_method_descriptor g_jack_controller_transport_iface_methods[] =
{
	JACK_DBUS_METHOD_DESCRIBE_END
};

JACK_DBUS_IFACE_DESCRIBE(
	g_jack_controller_iface_transport,
	"org.jackaudio.JackTransport",
	g_jack_controller_transport_iface_methods,
	NULL);
