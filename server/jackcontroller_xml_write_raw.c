/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2007,2008 Nedko Arnaudov
    
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

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dbus/dbus.h>

#include <jack/driver.h>
#include <jack/engine.h>
#include "jackcontroller_internal.h"
#include "jackdbus.h"

bool
jack_controller_settings_write_string(int fd, const char * string, void *dbus_call_context_ptr)
{
	size_t len;

	len = strlen(string);

	if (write(fd, string, len) != len)
	{
		jack_dbus_error(dbus_call_context_ptr, JACK_DBUS_ERROR_GENERIC, "write() failed to write config file.");
		return false;
	}

	return true;
}

struct save_context
{
	int fd;
	const char *indent;
};

#define save_context_ptr ((struct save_context *)context)
#define fd (save_context_ptr->fd)

bool
jack_controller_settings_write_option(
	void *context,
	const char *name,
	const char *content,
	void *dbus_call_context_ptr)
{
	if (!jack_controller_settings_write_string(fd, save_context_ptr->indent, dbus_call_context_ptr))
	{
		return false;
	}

	if (!jack_controller_settings_write_string(fd, "<option name=\"", dbus_call_context_ptr))
	{
		return false;
	}

	if (!jack_controller_settings_write_string(fd, name, dbus_call_context_ptr))
	{
		return false;
	}

	if (!jack_controller_settings_write_string(fd, "\">", dbus_call_context_ptr))
	{
		return false;
	}

	if (!jack_controller_settings_write_string(fd, content, dbus_call_context_ptr))
	{
		return false;
	}

	if (!jack_controller_settings_write_string(fd, "</option>\n", dbus_call_context_ptr))
	{
		return false;
	}

	return true;
}

#undef fd

bool
jack_controller_settings_save(
	struct jack_controller * controller_ptr,
	void *dbus_call_context_ptr)
{
	char *filename;
	size_t conf_len;
	int fd;
	bool ret;
	time_t timestamp;
	char timestamp_str[26];
	struct save_context context;
	const JSList * node_ptr;
	jackctl_driver driver;

	time(&timestamp);
	ctime_r(&timestamp, timestamp_str);
	timestamp_str[24] = 0;

	ret = false;

	conf_len = strlen(JACKDBUS_CONF);

	filename = malloc(g_jackdbus_config_dir_len + conf_len + 1);
	if (filename == NULL)
	{
		jack_error("Out of memory.");
		goto exit;
	}

	memcpy(filename, g_jackdbus_config_dir, g_jackdbus_config_dir_len);
	memcpy(filename + g_jackdbus_config_dir_len, JACKDBUS_CONF, conf_len);
	filename[g_jackdbus_config_dir_len + conf_len] = 0;

	jack_info("Saving settings to \"%s\" ...", filename);

	fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd == -1)
	{
		jack_error("open() failed to open conf filename.");
		goto exit_free_filename;
	}

	context.fd = fd;

	if (!jack_controller_settings_write_string(fd, "<?xml version=\"1.0\"?>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, "<!--\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, JACK_CONF_HEADER_TEXT, dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, "-->\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, "<!-- ", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, timestamp_str, dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, " -->\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, "<jack>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, " <engine>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	context.indent = "  ";
	if (!jack_controller_settings_save_engine_options(&context, controller_ptr, dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, " </engine>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, " <drivers>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	node_ptr = jackctl_server_get_drivers_list(controller_ptr->server);

	while (node_ptr != NULL)
	{
		driver = (jackctl_driver)node_ptr->data;

		if (!jack_controller_settings_write_string(fd, "  <driver name=\"", dbus_call_context_ptr))
		{
			goto exit_close;
		}

		if (!jack_controller_settings_write_string(fd, jackctl_driver_get_name(driver), dbus_call_context_ptr))
		{
			goto exit_close;
		}

		if (!jack_controller_settings_write_string(fd, "\">\n", dbus_call_context_ptr))
		{
			goto exit_close;
		}

		context.indent = "   ";

		if (!jack_controller_settings_save_driver_options(&context, driver, dbus_call_context_ptr))
		{
			goto exit_close;
		}

		if (!jack_controller_settings_write_string(fd, "  </driver>\n", dbus_call_context_ptr))
		{
			goto exit_close;
		}

		node_ptr = jack_slist_next(node_ptr);
	}

	if (!jack_controller_settings_write_string(fd, " </drivers>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	if (!jack_controller_settings_write_string(fd, "</jack>\n", dbus_call_context_ptr))
	{
		goto exit_close;
	}

	ret = true;

exit_close:
	close(fd);

exit_free_filename:
	free(filename);

exit:
	return ret;
}

void
jack_controller_settings_save_auto(
	struct jack_controller * controller_ptr)
{
	jack_controller_settings_save(controller_ptr, NULL);
}
