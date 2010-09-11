/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    JACK control API implementation

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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <assert.h>

#include <config.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/driver.h>

#include "../libjack/local.h"

#include "jackctl.h"

struct jackctl_server
{
	char * name;
	JSList * drivers;
	JSList * parameters;

	jack_engine_t * engine;

	unsigned int xruns;

	/* bool, whether to be "realtime" */
	union jackctl_parameter_value realtime;
	union jackctl_parameter_value default_realtime;

	/* int32_t */
	union jackctl_parameter_value realtime_priority;
	union jackctl_parameter_value default_realtime_priority;

	/* bool, if true - do not attempt to lock memory, even in realtime mode. */
	union jackctl_parameter_value no_mem_lock;
	union jackctl_parameter_value default_no_mem_lock;

	/* bool, whether to exit once all clients have closed their connections */
	union jackctl_parameter_value temporary;
	union jackctl_parameter_value default_temporary;

	/* bool, whether to be verbose */
	union jackctl_parameter_value verbose;
	union jackctl_parameter_value default_verbose;

	/* int32_t, msecs; if zero, use period size. */
	union jackctl_parameter_value client_timeout;
	union jackctl_parameter_value default_client_timeout;

	/* uint32_t, maximum number of ports the JACK server can manage */
	union jackctl_parameter_value port_max;
	union jackctl_parameter_value default_port_max;

	/* bool, whether to unlock libraries GTK+, QT, FLTK, Wine */
	union jackctl_parameter_value do_unlock;
	union jackctl_parameter_value default_do_unlock;

	/* int32_t */
	union jackctl_parameter_value frame_time_offset;
	union jackctl_parameter_value default_frame_time_offset;

	/* bool, whether to prevent from ever kicking out clients because they were too slow */
	union jackctl_parameter_value nozombies;
	union jackctl_parameter_value default_nozombies;

	/* char, clock source */
	union jackctl_parameter_value clock_source;
	union jackctl_parameter_value default_clock_source;

	/* bool, whether to remove the shared memory registry used by all JACK server instances before startup */
	union jackctl_parameter_value replace_registry;
	union jackctl_parameter_value default_replace_registry;

	uint64_t next_client_id;
	uint64_t next_port_id;
	uint64_t next_connection_id;
	void * patchbay_context;
	jackctl_client_appeared_callback client_appeared_callback;
	jackctl_client_disappeared_callback client_disappeared_callback;
	jackctl_port_appeared_callback port_appeared_callback;
	jackctl_port_disappeared_callback port_disappeared_callback;
	jackctl_ports_connected_callback ports_connected_callback;
	jackctl_ports_disconnected_callback ports_disconnected_callback;

	JSList * clients;
	JSList * connections;
};

struct jackctl_driver
{
	char * filename;
	jack_driver_desc_t * desc_ptr;
	JSList * parameters;
	JSList * set_parameters;
};

struct jackctl_parameter
{
	const char * name;
	const char * short_description;
	const char * long_description;
	jackctl_param_type_t type;
	bool is_set;
	union jackctl_parameter_value * value_ptr;
	union jackctl_parameter_value * default_value_ptr;

	union jackctl_parameter_value value;
	union jackctl_parameter_value default_value;
	struct jackctl_driver * driver_ptr;
	char id;
	jack_driver_param_t * driver_parameter_ptr;
};

struct jackctl_client
{
	uint64_t id;
	char * name;
	pid_t pid;
	JSList * ports;
	void * patchbay_context;
};

struct jackctl_port
{
	uint64_t id;
	char * name;
	uint32_t flags;
	uint32_t type;
	struct jackctl_client * client_ptr;
	void * patchbay_context;
};

struct jackctl_connection
{
	uint64_t id;
	struct jackctl_port * port1_ptr;
	struct jackctl_port * port2_ptr;
	void * patchbay_context;
};

static
struct jackctl_parameter *
jackctl_add_parameter(
	JSList ** parameters_list_ptr_ptr,
	const char * name,
	const char * short_description,
	const char * long_description,
	jackctl_param_type_t type,
	union jackctl_parameter_value * value_ptr,
	union jackctl_parameter_value * default_value_ptr,
	union jackctl_parameter_value value)
{
	struct jackctl_parameter * parameter_ptr;

	parameter_ptr = malloc(sizeof(struct jackctl_parameter));
	if (parameter_ptr == NULL)
	{
		jack_error("Cannot allocate memory for jackctl_parameter structure.");
		goto fail;
	}

	parameter_ptr->name = name;
	parameter_ptr->short_description = short_description;
	parameter_ptr->long_description = long_description;
	parameter_ptr->type = type;
	parameter_ptr->is_set = false;

	if (value_ptr == NULL)
	{
		value_ptr = &parameter_ptr->value;
	}

	if (default_value_ptr == NULL)
	{
		default_value_ptr = &parameter_ptr->default_value;
	}

	parameter_ptr->value_ptr = value_ptr;
	parameter_ptr->default_value_ptr = default_value_ptr;

	*value_ptr = *default_value_ptr = value;

	parameter_ptr->driver_ptr = NULL;
	parameter_ptr->driver_parameter_ptr = NULL;
	parameter_ptr->id = 0;

	*parameters_list_ptr_ptr = jack_slist_append(*parameters_list_ptr_ptr, parameter_ptr);

	return parameter_ptr;

fail:
	return NULL;
}

static
void
jackctl_free_driver_parameters(
	struct jackctl_driver * driver_ptr)
{
	JSList * next_node_ptr;

	while (driver_ptr->parameters)
	{
		next_node_ptr = driver_ptr->parameters->next;
		free(driver_ptr->parameters->data);
		free(driver_ptr->parameters);
		driver_ptr->parameters = next_node_ptr;
	}

	while (driver_ptr->set_parameters)
	{
		next_node_ptr = driver_ptr->set_parameters->next;
		free(driver_ptr->set_parameters->data);
		free(driver_ptr->set_parameters);
		driver_ptr->set_parameters = next_node_ptr;
	}
}

static
bool
jackctl_add_driver_parameters(
	struct jackctl_driver * driver_ptr)
{
	uint32_t i;
	union jackctl_parameter_value jackctl_value;
	jackctl_param_type_t jackctl_type;
	struct jackctl_parameter * parameter_ptr;
	jack_driver_param_desc_t * descriptor_ptr;

	for (i = 0 ; i < driver_ptr->desc_ptr->nparams ; i++)
	{
		descriptor_ptr = driver_ptr->desc_ptr->params + i;

		switch (descriptor_ptr->type)
		{
		case JackDriverParamInt:
			jackctl_type = JackParamInt;
			jackctl_value.i = descriptor_ptr->value.i;
			break;
		case JackDriverParamUInt:
			jackctl_type = JackParamUInt;
			jackctl_value.ui = descriptor_ptr->value.ui;
			break;
		case JackDriverParamChar:
			jackctl_type = JackParamChar;
			jackctl_value.c = descriptor_ptr->value.c;
			break;
		case JackDriverParamString:
			jackctl_type = JackParamString;
			strcpy(jackctl_value.str, descriptor_ptr->value.str);
			break;
		case JackDriverParamBool:
			jackctl_type = JackParamBool;
			jackctl_value.b = descriptor_ptr->value.i;
			break;
		default:
			jack_error("unknown driver parameter type %i", (int)descriptor_ptr->type);
			assert(0);
			goto fail;
		}

		parameter_ptr = jackctl_add_parameter(
			&driver_ptr->parameters,
			descriptor_ptr->name,
			descriptor_ptr->short_desc,
			descriptor_ptr->long_desc,
			jackctl_type,
			NULL,
			NULL,
			jackctl_value);

		if (parameter_ptr == NULL)
		{
			goto fail;
		}

		parameter_ptr->driver_ptr = driver_ptr;
		parameter_ptr->id = descriptor_ptr->character;
	}

	return true;

fail:
	jackctl_free_driver_parameters(driver_ptr);

	return false;
}

static
bool
jackctl_load_driver_descriptor(
	struct jackctl_server * server_ptr,
	struct jackctl_driver * driver_ptr)
{
	jack_driver_desc_t * descriptor;
	JackDriverDescFunction so_get_descriptor;
	void * dlhandle;
	const char * dlerr;
	int err;

	if (server_ptr->verbose.b) {
		jack_info ("getting driver descriptor from %s", driver_ptr->filename);
	}

	dlhandle = dlopen(driver_ptr->filename, RTLD_NOW|RTLD_GLOBAL);
	if (dlhandle == NULL) {
		jack_error("could not open driver .so '%s': %s", driver_ptr->filename, dlerror());
		return false;
	}

	so_get_descriptor = (JackDriverDescFunction)
		dlsym(dlhandle, "driver_get_descriptor");

	dlerr = dlerror();
	if (dlerr != NULL) {
		jack_error("cannot find driver_get_descriptor symbol: %s", dlerr);
		dlclose(dlhandle);
		return false;
	}

	descriptor = so_get_descriptor();
	if (descriptor == NULL) {
		jack_error("driver from '%s' returned NULL descriptor", driver_ptr->filename);
		dlclose(dlhandle);
		return false;
	}

	err = dlclose(dlhandle);
	if (err != 0) {
		jack_error("error closing driver .so '%s': %s", driver_ptr->filename, dlerror());
		free(descriptor->params);
		free(descriptor);
		return false;
	}

	/* for some mad reason we are storing filename in descriptor allocated by driver
	   instead of reusing dlhandle when another dlsym() call is needed */
	snprintf (descriptor->file, sizeof(descriptor->file), "%s", driver_ptr->filename);

	driver_ptr->desc_ptr = descriptor;

	return true;
}

static int
jack_drivers_load(
	struct jackctl_server * server_ptr)
{
	struct dirent * dir_entry;
	DIR * dir_stream;
	const char * ptr;
	int err;
	char* driver_dir;
	struct jackctl_driver * driver_ptr;
	struct jackctl_driver * other_driver_ptr;
	JSList * node_ptr;
	unsigned int drivers_count;

	if ((driver_dir = getenv("JACK_DRIVER_DIR")) == 0) {
		driver_dir = ADDON_DIR;
	}

	if (server_ptr->verbose.b) {
		jack_info ("searching for drivers in %s", driver_dir);
	}

	/* search through the driver_dir and add get descriptors
	   from the .so files in it */
	dir_stream = opendir (driver_dir);
	if (!dir_stream) {
		jack_error ("could not open driver directory %s: %s",
			    driver_dir, strerror (errno));
		return false;
	}

	drivers_count = 0;

	while ( (dir_entry = readdir (dir_stream)) ) {
		/* check the filename is of the right format */
		if (strncmp ("jack_", dir_entry->d_name, 5) != 0) {
			continue;
		}

#if SETTINGS_PERSISTENCE_USE_LIBXML2
		/* disable ffado driver.
		   it is incompatible with using libxml2 by other module,
		   be it jackdbus or some other driver.
		   libxml2 has global hooks used by libxml++, used by libffado */
		if (strcmp ("jack_firewire.so", dir_entry->d_name) == 0) {
			continue;
		}
#endif

		ptr = strrchr (dir_entry->d_name, '.');
		if (!ptr) {
			continue;
		}
		ptr++;
		if (strncmp ("so", ptr, 2) != 0) {
			continue;
		}

		driver_ptr = malloc(sizeof(struct jackctl_driver));
		if (driver_ptr == NULL)
		{
			jack_error("memory allocation of jackctl_driver structure failed.");
			continue;
		}

		driver_ptr->filename = malloc(strlen(driver_dir) + 1 + strlen(dir_entry->d_name) + 1);

		sprintf(driver_ptr->filename, "%s/%s", driver_dir, dir_entry->d_name);

		if (!jackctl_load_driver_descriptor(server_ptr, driver_ptr))
		{
			goto dealloc_driver;
		}

		/* check it doesn't exist already */
		for (node_ptr = server_ptr->drivers; node_ptr != NULL; node_ptr = jack_slist_next(node_ptr))
		{
			other_driver_ptr = (struct jackctl_driver *)node_ptr->data;

			if (strcmp(driver_ptr->desc_ptr->name, other_driver_ptr->desc_ptr->name) == 0)
			{
				jack_error(
					"the drivers in '%s' and '%s' both have the name '%s'; using the first",
					other_driver_ptr->filename,
					driver_ptr->filename,
					driver_ptr->desc_ptr->name);
				goto dealloc_descriptor;
			}
		}

		driver_ptr->parameters = NULL;
		driver_ptr->set_parameters = NULL;

		if (!jackctl_add_driver_parameters(driver_ptr))
		{
			assert(driver_ptr->parameters == NULL);
			goto dealloc_descriptor;
		}

		server_ptr->drivers = jack_slist_append(server_ptr->drivers, driver_ptr);
		drivers_count++;

		continue;

	dealloc_descriptor:
		free(driver_ptr->desc_ptr->params);
		free(driver_ptr->desc_ptr);

	dealloc_driver:
		free(driver_ptr->filename);
		free(driver_ptr);
	}

	err = closedir (dir_stream);
	if (err) {
		jack_error ("error closing driver directory %s: %s",
			    driver_dir, strerror (errno));
	}

	if (drivers_count == 0)
	{
		jack_error ("could not find any drivers in %s!", driver_dir);
		return false;
	}

	return true;
}

static
void
jackctl_server_free_drivers(
	struct jackctl_server * server_ptr)
{
	JSList * next_node_ptr;
	struct jackctl_driver * driver_ptr;

	while (server_ptr->drivers)
	{
		next_node_ptr = server_ptr->drivers->next;
		driver_ptr = (struct jackctl_driver *)server_ptr->drivers->data;

		jackctl_free_driver_parameters(driver_ptr);
		free(driver_ptr->desc_ptr->params);
		free(driver_ptr->desc_ptr);
		free(driver_ptr->filename);
		free(driver_ptr);

		free(server_ptr->drivers);
		server_ptr->drivers = next_node_ptr;
	}
}

static
void
jackctl_server_free_parameters(
	struct jackctl_server * server_ptr)
{
	JSList * next_node_ptr;

	while (server_ptr->parameters)
	{
		next_node_ptr = server_ptr->parameters->next;
		free(server_ptr->parameters->data);
		free(server_ptr->parameters);
		server_ptr->parameters = next_node_ptr;
	}
}

static void
jack_cleanup_files (const char *server_name)
{
	DIR *dir;
	struct dirent *dirent;
	char dir_name[PATH_MAX+1] = "";
        jack_server_dir (server_name, dir_name);

	/* On termination, we remove all files that jackd creates so
	 * subsequent attempts to start jackd will not believe that an
	 * instance is already running.  If the server crashes or is
	 * terminated with SIGKILL, this is not possible.  So, cleanup
	 * is also attempted when jackd starts.
	 *
	 * There are several tricky issues.  First, the previous JACK
	 * server may have run for a different user ID, so its files
	 * may be inaccessible.  This is handled by using a separate
	 * JACK_TMP_DIR subdirectory for each user.  Second, there may
	 * be other servers running with different names.  Each gets
	 * its own subdirectory within the per-user directory.  The
	 * current process has already registered as `server_name', so
	 * we know there is no other server actively using that name.
	 */

	/* nothing to do if the server directory does not exist */
	if ((dir = opendir (dir_name)) == NULL) {
		return;
	}

	/* unlink all the files in this directory, they are mine */
	while ((dirent = readdir (dir)) != NULL) {

		char fullpath[PATH_MAX+1];

		if ((strcmp (dirent->d_name, ".") == 0)
		    || (strcmp (dirent->d_name, "..") == 0)) {
			continue;
		}

		snprintf (fullpath, sizeof (fullpath), "%s/%s",
			  dir_name, dirent->d_name);

		if (unlink (fullpath)) {
			jack_error ("cannot unlink `%s' (%s)", fullpath,
				    strerror (errno));
		}
	} 

	closedir (dir);

	/* now, delete the per-server subdirectory, itself */
	if (rmdir (dir_name)) {
 		jack_error ("cannot remove `%s' (%s)", dir_name,
			    strerror (errno));
	}

	/* finally, delete the per-user subdirectory, if empty */
	if (rmdir (jack_user_dir ())) {
		if (errno != ENOTEMPTY) {
			jack_error ("cannot remove `%s' (%s)",
				    jack_user_dir (), strerror (errno));
		}
	}
}

static
int
jackctl_xrun(void *arg)
{
	((struct jackctl_server *)arg)->xruns++;

	return 0;
}

jackctl_server jackctl_server_create(const char * name)
{
	struct jackctl_server * server_ptr;
	union jackctl_parameter_value value;

	server_ptr = malloc(sizeof(struct jackctl_server));
	if (server_ptr == NULL)
	{
		jack_error("Cannot allocate memory for jackctl_server structure.");
		goto fail;
	}

	if (name != NULL)
	{
		server_ptr->name = strdup(name);
	}
	else
	{
		server_ptr->name = strdup(jack_default_server_name());
	}

	if (server_ptr->name == NULL)
	{
		goto fail_free_server;
	}

	server_ptr->drivers = NULL;
	server_ptr->parameters = NULL;

	server_ptr->engine = NULL;
	server_ptr->xruns = 0;
	server_ptr->next_client_id = 1;
	server_ptr->next_port_id = 1;
	server_ptr->next_connection_id = 1;

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "realtime",
		    "Whether to use realtime mode",
		    "Use realtime scheduling. This is needed for reliable low-latency performance. On most systems, it requires JACK to run with special scheduler and memory allocation privileges, which may be obtained in several ways. On Linux you should use PAM.",
		    JackParamBool,
		    &server_ptr->realtime,
		    &server_ptr->default_realtime,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.i = 10;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "realtime-priority",
		    "Scheduler priority when running in realtime mode.",
		    "",
		    JackParamInt,
		    &server_ptr->realtime_priority,
		    &server_ptr->default_realtime_priority,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "no-mem-lock",
		    "Do not attempt to lock memory, even in realtime mode.",
		    "",
		    JackParamBool,
		    &server_ptr->no_mem_lock,
		    &server_ptr->default_no_mem_lock,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "temporary",
		    "Exit once all clients have closed their connections.",
		    "",
		    JackParamBool,
		    &server_ptr->temporary,
		    &server_ptr->default_temporary,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "verbose",
		    "Verbose mode.",
		    "",
		    JackParamBool,
		    &server_ptr->verbose,
		    &server_ptr->default_verbose,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.i = 500;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "client-timeout",
		    "Client timeout limit in milliseconds",
		    "Client timeout limit in milliseconds. In realtime mode the client timeout must be smaller than the watchdog timeout (5000 msec).",
		    JackParamInt,
		    &server_ptr->client_timeout,
		    &server_ptr->default_client_timeout,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "no-zombies",
		    "Prevent JACK from ever kicking out clients because they were too slow.",
		    "Prevent JACK from ever kicking out clients because they were too slow. JACK and its clients are still subject to the supervision of the watchdog thread or its equivalent.",
		    JackParamBool,
		    &server_ptr->nozombies,
		    &server_ptr->default_nozombies,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.ui = 256;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "port-max",
		    "Maximum number of ports the JACK server can manage",
		    "",
		    JackParamUInt,
		    &server_ptr->port_max,
		    &server_ptr->default_port_max,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "libs-unlock",
		    "Unlock libraries GTK+, QT, FLTK, Wine.",
		    "",
		    JackParamBool,
		    &server_ptr->do_unlock,
		    &server_ptr->default_do_unlock,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.c = 's';
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "clock-source",
		    "Clock source",
		    "Select a specific wall clock.\n"
		    "  'c' - Cycle Counter\n"
		    "  'h' - HPET timer\n"
		    "  's' - System timer\n",
		    JackParamChar,
		    &server_ptr->clock_source,
		    &server_ptr->default_clock_source,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.i = 0;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "debug-timer",
		    "Debug timer",
		    "",
		    JackParamInt,
		    &server_ptr->frame_time_offset,
		    &server_ptr->default_frame_time_offset,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	value.b = false;
	if (jackctl_add_parameter(
		    &server_ptr->parameters,
		    "replace-registry",
		    "Replace registry.",
		    "Remove the shared memory registry used by all JACK server instances before startup. This should rarely be used, and is intended only for occasions when the structure of this registry changes in ways that are incompatible across JACK versions (which is rare).",
		    JackParamBool,
		    &server_ptr->replace_registry,
		    &server_ptr->default_replace_registry,
		    value) == NULL)
	{
		goto fail_free_name;
	}

	if (!jack_drivers_load(server_ptr))
	{
		goto fail_free_parameters;
	}

	return (jackctl_server)server_ptr;

fail_free_parameters:
	jackctl_server_free_parameters(server_ptr);

fail_free_name:
	free(server_ptr->name);

fail_free_server:
	free(server_ptr);

fail:
	return NULL;
}

static
struct jackctl_client *
jackctl_find_client(
	struct jackctl_server * server_ptr,
	const char * client_name, /* not '\0' terminated */
	size_t client_name_len)	/* without terminating '\0' */
{
	JSList * node_ptr;
	struct jackctl_client * client_ptr;

	node_ptr = server_ptr->clients;

	while (node_ptr != NULL)
	{
		client_ptr = node_ptr->data;
		if (strncmp(client_ptr->name, client_name, client_name_len) == 0)
		{
			return client_ptr;
		}

		node_ptr = jack_slist_next(node_ptr);
	}

	return NULL;
}

static
struct jackctl_client *
jackctl_find_or_create_client(
	struct jackctl_server * server_ptr,
	const char * client_name, /* not '\0' terminated */
	size_t client_name_len,	/* without terminating '\0' */
	pid_t pid)
{
	struct jackctl_client * client_ptr;

	/* First, try to find existing client */
	client_ptr = jackctl_find_client(server_ptr, client_name, client_name_len);
	if (client_ptr != NULL)
	{
		return client_ptr;
	}

	/* Such client does not exist, create new one */

	client_ptr = malloc(sizeof(struct jackctl_client));
	if (client_ptr == NULL)
	{
		jack_error("Allocation of jackctl_client structure failed.");
		goto fail;
	}

	client_ptr->name = malloc((client_name_len + 1) * sizeof(char));
	if (client_ptr->name == NULL)
	{
		jack_error("Allocation of client name string of %u chars failed.", (unsigned int)client_name_len);
		goto fail_free_client;
	}

	memcpy(client_ptr->name, client_name, client_name_len * sizeof(char));
	client_ptr->name[client_name_len] = '\0';

	client_ptr->ports = NULL;

	client_ptr->id = server_ptr->next_client_id++;

	client_ptr->pid = pid;

	server_ptr->clients = jack_slist_append(server_ptr->clients, client_ptr);

	if (server_ptr->client_appeared_callback != NULL)
	{
		client_ptr->patchbay_context = server_ptr->client_appeared_callback(
			server_ptr->patchbay_context,
			client_ptr->id,
			client_ptr->name);

	}


	return client_ptr;

fail_free_client:
	free(client_ptr);

fail:
	return NULL;
}

static
struct jackctl_port *
jackctl_find_port(
	struct jackctl_client * client_ptr,
	const char * port_name)	/* '\0' terminated */
{
	JSList * node_ptr;
	struct jackctl_port * port_ptr;

	node_ptr = client_ptr->ports;

	while (node_ptr != NULL)
	{
		port_ptr = node_ptr->data;
		if (strcmp(port_ptr->name, port_name) == 0)
		{
			return port_ptr;
		}

		node_ptr = jack_slist_next(node_ptr);
	}

	return NULL;
}

static
bool
jackctl_remove_port(
	struct jackctl_server * server_ptr,
	struct jackctl_client * client_ptr,
	const char * port_name)
{
	JSList * node_ptr;
	struct jackctl_port * port_ptr;

	node_ptr = client_ptr->ports;

	while (node_ptr != NULL)
	{
		port_ptr = node_ptr->data;
		if (strcmp(port_ptr->name, port_name) == 0)
		{
			goto found;
		}

		node_ptr = jack_slist_next(node_ptr);
	}

	jack_error("Unknown port '%s' of client '%s'", port_name, client_ptr->name);

	return false;

found:
	client_ptr->ports = jack_slist_remove(client_ptr->ports, port_ptr);

	if (server_ptr->port_disappeared_callback != NULL)
	{
		server_ptr->port_disappeared_callback(
			server_ptr->patchbay_context,
			client_ptr->id,
			client_ptr->patchbay_context,
			port_ptr->id,
			port_ptr->patchbay_context);

	}

	free(port_ptr->name);
	free(port_ptr);

	if (client_ptr->ports == NULL)
	{
		/* the last port of the client, remove the client */

		server_ptr->clients = jack_slist_remove(server_ptr->clients, client_ptr);

		if (server_ptr->client_disappeared_callback != NULL)
		{
			server_ptr->client_disappeared_callback(
				server_ptr->patchbay_context,
				client_ptr->id,
				client_ptr->patchbay_context);

		}

		free(client_ptr->name);
		free(client_ptr);
	}

	return true;
}

#define server_ptr ((struct jackctl_server *)server)

int
jack_port_do_connect(
	jack_engine_t *engine,
	const char *source_port,
	const char *destination_port);

int
jack_port_do_disconnect(
	jack_engine_t *engine,
	const char *source_port,
	const char *destination_port);

bool
jackctl_connect_ports_by_name(
	jackctl_server server,
	const char * client1_name,
	const char * port1_name,
	const char * client2_name,
	const char * port2_name)
{
	int ret;
	char port1_full_name[JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE];
	char port2_full_name[JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE];

	if (strlen(client1_name) + strlen(port1_name) + 2 > JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE ||
	    strlen(client2_name) + strlen(port2_name) + 2 > JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE)
	{
		jack_error("client name + port name too long");
		return false;
	}

	sprintf(port1_full_name, "%s:%s", client1_name, port1_name);
	sprintf(port2_full_name, "%s:%s", client2_name, port2_name);

	ret = jack_port_do_connect(
		server_ptr->engine,
		port1_full_name,
		port2_full_name);

	if (ret != 0)
	{
		jack_error("jack_port_do_connect('%s', '%s') failed with %d", port1_full_name, port2_full_name, ret);
		return false;
	}

	return true;
}

bool
jackctl_disconnect_ports_by_name(
	jackctl_server server,
	const char * client1_name,
	const char * port1_name,
	const char * client2_name,
	const char * port2_name)
{
	int ret;
	char port1_full_name[JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE];
	char port2_full_name[JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE];

	if (strlen(client1_name) + strlen(port1_name) + 2 > JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE ||
	    strlen(client2_name) + strlen(port2_name) + 2 > JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE)
	{
		jack_error("client name + port name too long");
		return false;
	}

	sprintf(port1_full_name, "%s:%s", client1_name, port1_name);
	sprintf(port2_full_name, "%s:%s", client2_name, port2_name);

	ret = jack_port_do_disconnect(
		server_ptr->engine,
		port1_full_name,
		port2_full_name);

	if (ret != 0)
	{
		jack_error("jack_port_do_disconnect('%s', '%s') failed with %d", port1_full_name, port2_full_name, ret);
		return false;
	}

	return true;
}

bool
jackctl_connect_ports_by_id(
	jackctl_server server,
	uint64_t port1_id,
	uint64_t port2_id)
{
	jack_info("Connecting %"PRIu64" to %"PRIu64, port1_id, port2_id);
	jack_error("jackctl_connect_ports_by_id() not implemetned yet");
	return false;
}

bool
jackctl_disconnect_ports_by_id(
	jackctl_server server,
	uint64_t port1_id,
	uint64_t port2_id)
{
	jack_info("Disconnecting %"PRIu64" from %"PRIu64, port1_id, port2_id);
	jack_error("jackctl_disconnect_ports_by_id() not implemetned yet");
	return false;
}

bool
jackctl_disconnect_ports_by_connection_id(
	jackctl_server server,
	uint64_t connection_id)
{
	jack_info("Disconnecting connection %"PRIu64, connection_id);
	jack_error("jackctl_disconnect_ports_by_connection_id() not implemetned yet");
	return false;
}

void jackctl_server_destroy(jackctl_server server)
{
	jackctl_server_free_drivers(server_ptr);
	jackctl_server_free_parameters(server_ptr);
	free(server_ptr->name);
	free(server_ptr);
}

const JSList * jackctl_server_get_drivers_list(jackctl_server server)
{
	return server_ptr->drivers;
}

bool jackctl_server_stop(jackctl_server server)
{
	JSList * next_client_node_ptr;
	struct jackctl_client * client_ptr;
	JSList * next_port_node_ptr;
	struct jackctl_port * port_ptr;

	jack_engine_delete(server_ptr->engine);

	/* clean up shared memory and files from this server instance */
	if (server_ptr->verbose.b)
	{
		jack_info("cleaning up shared memory");
	}

	jack_cleanup_shm();

	if (server_ptr->verbose.b)
	{
		jack_info("cleaning up files");
	}

	jack_cleanup_files(server_ptr->name);

	if (server_ptr->verbose.b)
	{
		jack_info("unregistering server `%s'", server_ptr->name);
	}

	jack_unregister_server(server_ptr->name);

	while (server_ptr->clients)
	{
		next_client_node_ptr = server_ptr->clients->next;

		client_ptr = server_ptr->clients->data;

		while (client_ptr->ports)
		{
			next_port_node_ptr = client_ptr->ports->next;
			port_ptr = client_ptr->ports->data;

			if (server_ptr->port_disappeared_callback != NULL)
			{
				server_ptr->port_disappeared_callback(
					server_ptr->patchbay_context,
					client_ptr->id,
					client_ptr->patchbay_context,
					port_ptr->id,
					port_ptr->patchbay_context);

			}

			free(port_ptr->name);
			free(port_ptr);

			free(client_ptr->ports);
			client_ptr->ports = next_port_node_ptr;
		}

		if (server_ptr->client_disappeared_callback != NULL)
		{
			server_ptr->client_disappeared_callback(
				server_ptr->patchbay_context,
				client_ptr->id,
				client_ptr->patchbay_context);

		}

		free(client_ptr->name);
		free(client_ptr);

		free(server_ptr->clients);
		server_ptr->clients = next_client_node_ptr;
	}

	server_ptr->engine = NULL;

	return true;
}

double jackctl_server_get_load(jackctl_server server)
{
	return server_ptr->engine->control->cpu_load;
}

unsigned int jackctl_server_get_sample_rate(jackctl_server server)
{
	return server_ptr->engine->control->current_time.frame_rate;
}

double jackctl_server_get_latency(jackctl_server server)
{
	return server_ptr->engine->driver->period_usecs / 1000.0;
}

unsigned int
jackctl_server_get_buffer_size(
	jackctl_server server)
{
	return server_ptr->engine->control->buffer_size;
}

int
jack_set_buffer_size_request (jack_engine_t *engine, jack_nframes_t nframes);

bool
jackctl_server_set_buffer_size(
	jackctl_server server,
	unsigned int nframes)
{
	int ret;
	ret = jack_set_buffer_size_request(server_ptr->engine, nframes);
	if (ret != 0)
	{
		jack_error("jack_set_buffer_size_request() failed.");
		return false;
	}

	return true;
}

bool jackctl_server_is_realtime(jackctl_server server)
{
	return server_ptr->realtime.b;
}

unsigned int jackctl_server_get_xruns(jackctl_server server)
{
	return server_ptr->xruns;
}

void jackctl_server_reset_xruns(jackctl_server server)
{
	server_ptr->xruns = 0;
}

const JSList * jackctl_server_get_parameters(jackctl_server server)
{
	return server_ptr->parameters;
}

static
void
jackctl_port_registration_notify(
	void * server,
	jack_port_id_t port_id,
	int yn)
{
	const char * port_full_name;
	struct jackctl_client * client_ptr;
	const char * port_short_name;
	struct jackctl_port * port_ptr;
	jack_client_id_t client_id;
	jack_client_internal_t * client;
	pid_t pid;

/* 	jack_info("jackctl_port_registration_notify() called."); */

	port_full_name = server_ptr->engine->control->ports[port_id].name;

	client_id = server_ptr->engine->control->ports[port_id].client_id;
	client = jack_client_internal_by_id(server_ptr->engine, client_id);
	if (client != NULL && client->control->type == ClientExternal)
	{
		pid = client->control->pid;
	}
	else
	{
		pid = 0;
	}

	port_short_name = strchr(port_full_name, ':');
	if (port_short_name == NULL)
	{
		jack_error("port name '%s' does not contain ':' separator char", port_full_name);
		return;
	}

	port_short_name++;	/* skip ':' separator char */

	if (yn)
	{
		/* appearing port */

		client_ptr = jackctl_find_or_create_client(server_ptr, port_full_name, port_short_name - port_full_name - 1, pid);
		if (client_ptr == NULL)
		{
			jack_error("Creation of new jackctl client failed.");
			return;
		}

		port_ptr = malloc(sizeof(struct jackctl_port));
		if (port_ptr == NULL)
		{
			jack_error("Allocation of jackctl_port structure failed.");
			return;
		}

		port_ptr->id = server_ptr->next_port_id++;
		port_ptr->name = strdup(port_short_name);
		port_ptr->client_ptr = client_ptr;
		port_ptr->flags = server_ptr->engine->control->ports[port_id].flags;
		port_ptr->type = server_ptr->engine->control->ports[port_id].ptype_id;

		client_ptr->ports = jack_slist_append(client_ptr->ports, port_ptr);

		if (server_ptr->port_appeared_callback != NULL)
		{
			port_ptr->patchbay_context = server_ptr->port_appeared_callback(
				server_ptr->patchbay_context,
				client_ptr->id,
				client_ptr->patchbay_context,
				port_ptr->id,
				port_ptr->name,
				port_ptr->flags,
				port_ptr->type);
		}

		return;
	}

	/* disappearing port */

	client_ptr = jackctl_find_client(server_ptr, port_full_name, port_short_name - port_full_name - 1);
	if (client_ptr == NULL)
	{
		jack_error("Port '%s' of unknown jackctl client disappeared.", port_full_name);
		return;
	}

	jackctl_remove_port(server_ptr, client_ptr, port_short_name);
}

void
jackctl_connection_notify(
	void * server,
	jack_port_id_t port1_id,
	jack_port_id_t port2_id,
	int connected)
{
	const char * port1_full_name;
	struct jackctl_client * client1_ptr;
	const char * port1_short_name;
	struct jackctl_port * port1_ptr;
	const char * port2_full_name;
	struct jackctl_client * client2_ptr;
	const char * port2_short_name;
	struct jackctl_port * port2_ptr;
	struct jackctl_connection * connection_ptr;
	JSList * node_ptr;

/*  	jack_info("jackctl_connection_notify() called."); */

	port1_full_name = server_ptr->engine->control->ports[port1_id].name;

	port1_short_name = strchr(port1_full_name, ':');
	if (port1_short_name == NULL)
	{
		jack_error("port name '%s' does not contain ':' separator char", port1_full_name);
		return;
	}

	port1_short_name++;	/* skip ':' separator char */

	port2_full_name = server_ptr->engine->control->ports[port2_id].name;

	port2_short_name = strchr(port2_full_name, ':');
	if (port2_short_name == NULL)
	{
		jack_error("port name '%s' does not contain ':' separator char", port2_full_name);
		return;
	}

	port2_short_name++;	/* skip ':' separator char */

	client1_ptr = jackctl_find_client(server_ptr, port1_full_name, port1_short_name - port1_full_name - 1);
	if (client1_ptr == NULL)
	{
		jack_error("Port '%s' of unknown jackctl client.", port1_full_name);
		return;
	}

	port1_ptr = jackctl_find_port(client1_ptr, port1_short_name);
	if (port1_ptr == NULL)
	{
		jack_error("Unknown port '%s'.", port1_full_name);
		return;
	}

	client2_ptr = jackctl_find_client(server_ptr, port2_full_name, port2_short_name - port2_full_name - 1);
	if (client2_ptr == NULL)
	{
		jack_error("Port '%s' of unknown jackctl client.", port2_full_name);
		return;
	}

	port2_ptr = jackctl_find_port(client2_ptr, port2_short_name);
	if (port2_ptr == NULL)
	{
		jack_error("Unknown port '%s'.", port2_full_name);
		return;
	}

	if (connected && server_ptr->ports_connected_callback != NULL)
	{
		connection_ptr = malloc(sizeof(struct jackctl_connection));
		if (connection_ptr == NULL)
		{
			jack_error("Allocation of jackctl_connection structure failed.");
			return;
		}

		connection_ptr->id = server_ptr->next_connection_id++;
		connection_ptr->port1_ptr = port1_ptr;
		connection_ptr->port2_ptr = port2_ptr;

		server_ptr->connections = jack_slist_append(server_ptr->connections, connection_ptr);

		connection_ptr->patchbay_context = server_ptr->ports_connected_callback(
			server_ptr->patchbay_context,
			client1_ptr->id,
			client1_ptr->patchbay_context,
			port1_ptr->id,
			port1_ptr->patchbay_context,
			client2_ptr->id,
			client2_ptr->patchbay_context,
			port2_ptr->id,
			port2_ptr->patchbay_context,
			connection_ptr->id);
	}

	if (!connected && server_ptr->ports_disconnected_callback != NULL)
	{
		node_ptr = server_ptr->connections;

		while (node_ptr != NULL)
		{
			connection_ptr = node_ptr->data;
			if ((connection_ptr->port1_ptr == port1_ptr &&
			     connection_ptr->port2_ptr == port2_ptr) ||
			    (connection_ptr->port1_ptr == port2_ptr &&
			     connection_ptr->port2_ptr == port1_ptr))
			{
				server_ptr->connections = jack_slist_remove(server_ptr->connections, connection_ptr);

				server_ptr->ports_disconnected_callback(
					server_ptr->patchbay_context,
					client1_ptr->id,
					client1_ptr->patchbay_context,
					port1_ptr->id,
					port1_ptr->patchbay_context,
					client2_ptr->id,
					client2_ptr->patchbay_context,
					port2_ptr->id,
					port2_ptr->patchbay_context,
					connection_ptr->id,
					connection_ptr->patchbay_context);
				return;
			}

			node_ptr = jack_slist_next(node_ptr);
		}

		jack_error("Cannot find connection being removed");
	}
}

#define driver_ptr ((struct jackctl_driver *)driver)

bool
jackctl_server_start(
	jackctl_server server,
	jackctl_driver driver,
	void * context,
	jackctl_client_appeared_callback client_appeared_callback,
	jackctl_client_disappeared_callback client_disappeared_callback,
	jackctl_port_appeared_callback port_appeared_callback,
	jackctl_port_disappeared_callback port_disappeared_callback,
	jackctl_ports_connected_callback ports_connected_callback,
	jackctl_ports_disconnected_callback ports_disconnected_callback)
{
	int rc;

	switch (server_ptr->clock_source.c)
	{
	case 'h':
		jack_info("Using HPET timer as clock source.");
		clock_source = JACK_TIMER_HPET;
		break;
	case 'c':
		jack_info("Using Cycle Counter as clock source.");
		clock_source = JACK_TIMER_CYCLE_COUNTER;
		break;
	case 's':
		jack_info("Using System timer as clock source.");
		clock_source = JACK_TIMER_SYSTEM_CLOCK;
		break;
	default:
		jack_error(
			"Invalid value '%c' for clock source, "
			"valid values are "
			"'c' for Cycle Counter, "
			"'h' for HPET timer and "
			"'s' for System timer",
			server_ptr->clock_source.c);
		return false;
	}

	rc = jack_register_server(server_ptr->name, server_ptr->replace_registry.b);
	switch (rc)
	{
	case EEXIST:
		jack_error("`%s' server already active", server_ptr->name);
		goto fail;
	case ENOSPC:
		jack_error("too many servers already active");
		goto fail;
	case ENOMEM:
		jack_error("no access to shm registry");
		goto fail;
	}

	if (server_ptr->verbose.b)
	{
		jack_info ("server `%s' registered", server_ptr->name);
	}

	/* clean up shared memory and files from any previous
	 * instance of this server name */
	jack_cleanup_shm();
	jack_cleanup_files(server_ptr->name);

	if (!server_ptr->realtime.b && server_ptr->client_timeout.i == 0)
		server_ptr->client_timeout.i = 500; /* 0.5 sec; usable when non realtime. */

	/* get the engine/driver started */

	server_ptr->engine = jack_engine_new(
		server_ptr->realtime.b,
		server_ptr->realtime_priority.i,
		!server_ptr->no_mem_lock.b,
		server_ptr->do_unlock.b,
		server_ptr->name,
		server_ptr->temporary.b,
		server_ptr->verbose.b,
		server_ptr->client_timeout.i,
		server_ptr->port_max.ui,
		getpid(),
		server_ptr->frame_time_offset.i,
		server_ptr->nozombies.b,
		NULL);
	if (server_ptr->engine == NULL)
	{
		jack_error("Cannot create engine!");
		goto fail_unregister_server;
	}

	server_ptr->engine->jackctl_port_registration_notify = jackctl_port_registration_notify;
	server_ptr->engine->jackctl_connection_notify = jackctl_connection_notify;
	server_ptr->engine->jackctl_context = server_ptr;

	server_ptr->patchbay_context = context;
	server_ptr->client_appeared_callback = client_appeared_callback;
	server_ptr->client_disappeared_callback = client_disappeared_callback;
	server_ptr->port_appeared_callback = port_appeared_callback;
	server_ptr->port_disappeared_callback = port_disappeared_callback;
	server_ptr->ports_connected_callback = ports_connected_callback;
	server_ptr->ports_disconnected_callback = ports_disconnected_callback;

	server_ptr->clients = NULL;
	server_ptr->connections = NULL;

	jack_info("loading driver \"%s\" ...", driver_ptr->desc_ptr->name);

	if (jack_engine_load_driver(server_ptr->engine, driver_ptr->desc_ptr, driver_ptr->set_parameters)) {
		jack_error("cannot load driver module %s", driver_ptr->desc_ptr->name);
		goto fail_delete_engine;
	}

	server_ptr->xruns = 0;
	server_ptr->engine->driver->internal_client->private_client->xrun = jackctl_xrun;
	server_ptr->engine->driver->internal_client->private_client->xrun_arg = server_ptr;

	if (server_ptr->engine->driver->start(server_ptr->engine->driver) != 0) {
		jack_error("cannot start \"%s\" driver", driver_ptr->desc_ptr->name);
		goto fail_delete_engine;
	}

	return true;

fail_unregister_server:
	if (server_ptr->verbose.b)
	{
		jack_info("cleaning up shared memory");
	}

	jack_cleanup_shm();

	if (server_ptr->verbose.b)
	{
		jack_info("cleaning up files");
	}

	jack_cleanup_files(server_ptr->name);

	if (server_ptr->verbose.b)
	{
		jack_info("unregistering server `%s'", server_ptr->name);
	}

	jack_unregister_server(server_ptr->name);

fail_delete_engine:
	jack_engine_delete(server_ptr->engine);
	server_ptr->engine = NULL;

fail:
	return false;
}

int
jackctl_get_client_pid(
	jackctl_server server,
	const char * name)
{
	struct jackctl_client * client_ptr;

	client_ptr = jackctl_find_client(server_ptr, name, strlen(name));
	if (client_ptr == NULL)
	{
		return 0;
	}

	return client_ptr->pid;
}

#undef server_ptr

const char * jackctl_driver_get_name(jackctl_driver driver)
{
	return driver_ptr->desc_ptr->name;
}

const JSList * jackctl_driver_get_parameters(jackctl_driver driver)
{
	return driver_ptr->parameters;
}

#undef driver_ptr

#define parameter_ptr ((struct jackctl_parameter *)parameter)

const char * jackctl_parameter_get_name(jackctl_parameter parameter)
{
	return parameter_ptr->name;
}

const char * jackctl_parameter_get_short_description(jackctl_parameter parameter)
{
	return parameter_ptr->short_description;
}

const char * jackctl_parameter_get_long_description(jackctl_parameter parameter)
{
	return parameter_ptr->long_description;
}

bool jackctl_parameter_has_range_constraint(jackctl_parameter parameter)
{
    return false;
    //return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_RANGE) != 0;
}

bool jackctl_parameter_has_enum_constraint(jackctl_parameter parameter)
{
    return false;
    //return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_RANGE) == 0;
}

uint32_t jackctl_parameter_get_enum_constraints_count(jackctl_parameter parameter)
{
#if 0
    if (!jackctl_parameter_has_enum_constraint(parameter_ptr))
    {
        return 0;
    }

    return parameter_ptr->constraint_ptr->constraint.enumeration.count;
#else
    return 0;
#endif
}

union jackctl_parameter_value jackctl_parameter_get_enum_constraint_value(jackctl_parameter parameter, uint32_t index)
{
    //jack_driver_param_value_t * value_ptr;
    union jackctl_parameter_value jackctl_value;

#if 0
    value_ptr = &parameter_ptr->constraint_ptr->constraint.enumeration.possible_values_array[index].value;

    switch (parameter_ptr->type)
    {
    case JackParamInt:
        jackctl_value.i = value_ptr->i;
        break;
    case JackParamUInt:
        jackctl_value.ui = value_ptr->ui;
        break;
    case JackParamChar:
        jackctl_value.c = value_ptr->c;
        break;
    case JackParamString:
        strcpy(jackctl_value.str, value_ptr->str);
        break;
    default:
#endif
        jack_error("bad driver parameter type %i (enum constraint)", (int)parameter_ptr->type);
        assert(0);
#if 0
    }
#endif

    return jackctl_value;
}

const char * jackctl_parameter_get_enum_constraint_description(jackctl_parameter parameter, uint32_t index)
{
    return "???";
    //return parameter_ptr->constraint_ptr->constraint.enumeration.possible_values_array[index].short_desc;
}

void jackctl_parameter_get_range_constraint(jackctl_parameter parameter, union jackctl_parameter_value * min_ptr, union jackctl_parameter_value * max_ptr)
{
#if 0
    switch (parameter_ptr->type)
    {
    case JackParamInt:
        min_ptr->i = parameter_ptr->constraint_ptr->constraint.range.min.i;
        max_ptr->i = parameter_ptr->constraint_ptr->constraint.range.max.i;
        return;
    case JackParamUInt:
        min_ptr->ui = parameter_ptr->constraint_ptr->constraint.range.min.ui;
        max_ptr->ui = parameter_ptr->constraint_ptr->constraint.range.max.ui;
        return;
    default:
#endif
        jack_error("bad driver parameter type %i (range constraint)", (int)parameter_ptr->type);
        assert(0);
#if 0
    }
#endif
}

bool jackctl_parameter_constraint_is_strict(jackctl_parameter parameter)
{
    return false;
    //return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_STRICT) != 0;
}

bool jackctl_parameter_constraint_is_fake_value(jackctl_parameter parameter)
{
    return false;
    //return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_FAKE_VALUE) != 0;
}

jackctl_param_type_t jackctl_parameter_get_type(jackctl_parameter parameter)
{
	return parameter_ptr->type;
}

bool jackctl_parameter_is_set(jackctl_parameter parameter)
{
	return parameter_ptr->is_set;
}

union jackctl_parameter_value jackctl_parameter_get_value(jackctl_parameter parameter)
{
	return *parameter_ptr->value_ptr;
}

bool jackctl_parameter_reset(jackctl_parameter parameter)
{
    if (!parameter_ptr->is_set)
    {
        return true;
    }

    parameter_ptr->is_set = false;

    *parameter_ptr->value_ptr = *parameter_ptr->default_value_ptr;

    return true;
}

bool jackctl_parameter_set_value(jackctl_parameter parameter, const union jackctl_parameter_value * value_ptr)
{
	bool new_driver_parameter;

	/* for driver parameters, set the parameter by adding jack_driver_param_t in the set_parameters list */
	if (parameter_ptr->driver_ptr != NULL)
	{
/* 		jack_info("setting driver parameter %p ...", parameter_ptr); */
		new_driver_parameter = parameter_ptr->driver_parameter_ptr == NULL;
		if (new_driver_parameter)
		{
/* 			jack_info("new driver parameter..."); */

			parameter_ptr->driver_parameter_ptr = malloc(sizeof(jack_driver_param_t));
			if (parameter_ptr->driver_parameter_ptr == NULL)
			{
				jack_error ("Allocation of jack_driver_param_t structure failed");
				return false;
			}

			parameter_ptr->driver_parameter_ptr->character = parameter_ptr->id;

			parameter_ptr->driver_ptr->set_parameters = jack_slist_append(parameter_ptr->driver_ptr->set_parameters, parameter_ptr->driver_parameter_ptr);
		}

		switch (parameter_ptr->type)
		{
		case JackParamInt:
			parameter_ptr->driver_parameter_ptr->value.i = value_ptr->i;
			break;
		case JackParamUInt:
			parameter_ptr->driver_parameter_ptr->value.ui = value_ptr->ui;
			break;
		case JackParamChar:
			parameter_ptr->driver_parameter_ptr->value.c = value_ptr->c;
			break;
		case JackParamString:
			strcpy(parameter_ptr->driver_parameter_ptr->value.str, value_ptr->str);
			break;
		case JackParamBool:
			parameter_ptr->driver_parameter_ptr->value.i = value_ptr->b;
			break;
		default:
			jack_error("unknown parameter type %i", (int)parameter_ptr->type);
			assert(0);

			if (new_driver_parameter)
			{
				parameter_ptr->driver_ptr->set_parameters = jack_slist_remove(parameter_ptr->driver_ptr->set_parameters, parameter_ptr->driver_parameter_ptr);
			}

			return false;
		}
	}

	parameter_ptr->is_set = true;
	*parameter_ptr->value_ptr = *value_ptr;

	return true;
}

union jackctl_parameter_value jackctl_parameter_get_default_value(jackctl_parameter parameter)
{
	return *parameter_ptr->default_value_ptr;
}

#undef parameter_ptr
