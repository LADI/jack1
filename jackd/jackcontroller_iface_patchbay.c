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

#include <jack/internal.h>

#include "jackdbus.h"
#include "jackcontroller_internal.h"
#include "../jack/list.h"

#define JACK_DBUS_IFACE_NAME "org.jackaudio.JackPatchbay"

struct jack_graph
{
	uint64_t version;
	struct list_head clients;
	struct list_head ports;
	struct list_head connections;
};

struct jack_graph_client
{
	uint64_t id;
	char * name;
	int pid;
	struct list_head siblings;
	struct list_head ports;
};

struct jack_graph_port
{
	uint64_t id;
	char * name;
	uint32_t flags;
	uint32_t type;
	struct list_head siblings_graph;
	struct list_head siblings_client;
	struct jack_graph_client * client;
};

struct jack_graph_connection
{
	uint64_t id;
	struct jack_graph_port * port1;
	struct jack_graph_port * port2;
	struct list_head siblings;
};

struct jack_controller_patchbay
{
	pthread_mutex_t lock;
	struct jack_graph graph;
};

bool
jack_controller_patchbay_init(
	struct jack_controller * controller_ptr)
{
	struct jack_controller_patchbay * patchbay_ptr;

	//jack_info("jack_controller_patchbay_init() called");

	patchbay_ptr = malloc(sizeof(struct jack_controller_patchbay));
	if (patchbay_ptr == NULL)
	{
		jack_error("Memory allocation of jack_controller_patchbay structure failed.");
		return false;
	}

	pthread_mutex_init(&patchbay_ptr->lock, NULL);
	INIT_LIST_HEAD(&patchbay_ptr->graph.clients);
	INIT_LIST_HEAD(&patchbay_ptr->graph.ports);
	INIT_LIST_HEAD(&patchbay_ptr->graph.connections);
	patchbay_ptr->graph.version = 1;

	controller_ptr->patchbay_context = patchbay_ptr;

	return true;
}

void
jack_controller_patchbay_send_signal_graph_changed(
	dbus_uint64_t new_graph_version)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"GraphChanged",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_client_appeared(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client_id,
	const char * client_name)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"ClientAppeared",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client_id,
		DBUS_TYPE_STRING,
		&client_name,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_client_disappeared(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client_id,
	const char * client_name)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"ClientDisappeared",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client_id,
		DBUS_TYPE_STRING,
		&client_name,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_port_appeared(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client_id,
	const char * client_name,
	dbus_uint64_t port_id,
	const char * port_name,
	dbus_uint32_t port_flags,
	dbus_uint32_t port_type)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"PortAppeared",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client_id,
		DBUS_TYPE_STRING,
		&client_name,
		DBUS_TYPE_UINT64,
		&port_id,
		DBUS_TYPE_STRING,
		&port_name,
		DBUS_TYPE_UINT32,
		&port_flags,
		DBUS_TYPE_UINT32,
		&port_type,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_port_disappeared(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client_id,
	const char * client_name,
	dbus_uint64_t port_id,
	const char * port_name)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"PortDisappeared",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client_id,
		DBUS_TYPE_STRING,
		&client_name,
		DBUS_TYPE_UINT64,
		&port_id,
		DBUS_TYPE_STRING,
		&port_name,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_ports_connected(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client1_id,
	const char * client1_name,
	dbus_uint64_t port1_id,
	const char * port1_name,
	dbus_uint64_t client2_id,
	const char * client2_name,
	dbus_uint64_t port2_id,
	const char * port2_name,
	dbus_uint64_t connection_id)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"PortsConnected",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client1_id,
		DBUS_TYPE_STRING,
		&client1_name,
		DBUS_TYPE_UINT64,
		&port1_id,
		DBUS_TYPE_STRING,
		&port1_name,
		DBUS_TYPE_UINT64,
		&client2_id,
		DBUS_TYPE_STRING,
		&client2_name,
		DBUS_TYPE_UINT64,
		&port2_id,
		DBUS_TYPE_STRING,
		&port2_name,
		DBUS_TYPE_UINT64,
		&connection_id,
		DBUS_TYPE_INVALID);
}

void
jack_controller_patchbay_send_signal_ports_disconnected(
	dbus_uint64_t new_graph_version,
	dbus_uint64_t client1_id,
	const char * client1_name,
	dbus_uint64_t port1_id,
	const char * port1_name,
	dbus_uint64_t client2_id,
	const char * client2_name,
	dbus_uint64_t port2_id,
	const char * port2_name,
	dbus_uint64_t connection_id)
{

	jack_dbus_send_signal(
		JACK_CONTROLLER_OBJECT_PATH,
		JACK_DBUS_IFACE_NAME,
		"PortsDisconnected",
		DBUS_TYPE_UINT64,
		&new_graph_version,
		DBUS_TYPE_UINT64,
		&client1_id,
		DBUS_TYPE_STRING,
		&client1_name,
		DBUS_TYPE_UINT64,
		&port1_id,
		DBUS_TYPE_STRING,
		&port1_name,
		DBUS_TYPE_UINT64,
		&client2_id,
		DBUS_TYPE_STRING,
		&client2_name,
		DBUS_TYPE_UINT64,
		&port2_id,
		DBUS_TYPE_STRING,
		&port2_name,
		DBUS_TYPE_UINT64,
		&connection_id,
		DBUS_TYPE_INVALID);
}

static
struct jack_graph_client *
jack_controller_patchbay_find_client_by_id(
    struct jack_controller_patchbay *patchbay_ptr,
    uint64_t id)
{
    struct list_head *node_ptr;
    struct jack_graph_client *client_ptr;

    list_for_each(node_ptr, &patchbay_ptr->graph.clients)
    {
        client_ptr = list_entry(node_ptr, struct jack_graph_client, siblings);
        if (client_ptr->id == id)
        {
            return client_ptr;
        }
    }

    return NULL;
}

#define patchbay_ptr ((struct jack_controller_patchbay *)((struct jack_controller *)server_context)->patchbay_context)

void *
jack_controller_patchbay_client_appeared_callback(
	void * server_context,
	uint64_t client_id,
	const char * client_name)
{
	struct jack_graph_client * client_ptr;

/* 	jack_info("new client: '%s' (%" PRIu64 ")", client_name, client_id); */

	client_ptr = malloc(sizeof(struct jack_graph_client));
	if (client_ptr == NULL)
	{
		jack_error("Memory allocation of jack_graph_client structure failed.");
		goto fail;
	}

	client_ptr->name = strdup(client_name);
	if (client_ptr->name == NULL)
	{
		jack_error("strdup() call for client name '%s' failed.", client_name);
		goto fail_free_client;
	}

	client_ptr->id = client_id;
	INIT_LIST_HEAD(&client_ptr->ports);

	client_ptr->pid = jackctl_get_client_pid(((struct jack_controller *)server_context)->server, client_ptr->name);
	jack_info("New client '%s' with PID %d", client_ptr->name, client_ptr->pid);

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_add_tail(&client_ptr->siblings, &patchbay_ptr->graph.clients);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_client_appeared(patchbay_ptr->graph.version, client_id, client_name);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	return client_ptr;

fail_free_client:
	free(client_ptr);

fail:
	return NULL;
}

#define client_ptr ((struct jack_graph_client *)client_context)

void
jack_controller_patchbay_client_disappeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context)
{
/* 	jack_info("client %" PRIu64 " gone", client_id); */

	if (client_ptr == NULL)
	{
		jack_error("Ignoring disappear of client that failed to appear.");
		return;
	}

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_del(&client_ptr->siblings);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_client_disappeared(patchbay_ptr->graph.version, client_id, client_ptr->name);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	free(client_ptr->name);
	free(client_ptr);
}

void *
jack_controller_patchbay_port_appeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	const char * port_name,
	uint32_t port_flags,
	uint32_t port_type)
{
	struct jack_graph_port * port_ptr;

/* 	jack_info( */
/* 		"new port: '%s' (%" PRIu64 "), flags 0x%" PRIX32 ", type %" PRIu32 ", of client %" PRIu64, */
/* 		port_name, */
/* 		port_id, */
/* 		port_flags, */
/* 		port_type, */
/* 		client_id); */

	port_ptr = malloc(sizeof(struct jack_graph_port));
	if (port_ptr == NULL)
	{
		jack_error("Memory allocation of jack_graph_port structure failed.");
		goto fail;
	}

	port_ptr->name = strdup(port_name);
	if (port_ptr->name == NULL)
	{
		jack_error("strdup() call for port name '%s' failed.", port_name);
		goto fail_free_client;
	}

	port_ptr->id = port_id;
	port_ptr->flags = port_flags;
	port_ptr->type = port_type;
	port_ptr->client = client_ptr;

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_add_tail(&port_ptr->siblings_client, &client_ptr->ports);
	list_add_tail(&port_ptr->siblings_graph, &patchbay_ptr->graph.ports);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_port_appeared(
		patchbay_ptr->graph.version,
		client_id,
		client_ptr->name,
		port_id,
		port_name,
		port_flags,
		port_type);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	return port_ptr;

fail_free_client:
	free(port_ptr);

fail:
	return NULL;
}

#define port_ptr ((struct jack_graph_port *)port_context)

void
jack_controller_patchbay_port_disappeared_callback(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	void * port_context)
{
/* 	jack_info("port %" PRIu64 " of client %" PRIu64 " gone", port_id, client_id); */

	if (port_ptr == NULL)
	{
		jack_error("Ignoring disappear of port that failed to appear.");
		return;
	}

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_del(&port_ptr->siblings_client);
	list_del(&port_ptr->siblings_graph);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_port_disappeared(patchbay_ptr->graph.version, client_id, client_ptr->name, port_id, port_ptr->name);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	free(port_ptr->name);
	free(port_ptr);
}

#undef port_ptr
#undef client_ptr

#define port1_ptr ((struct jack_graph_port *)port1_context)
#define port2_ptr ((struct jack_graph_port *)port2_context)

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
	uint64_t connection_id)
{
	struct jack_graph_connection * connection_ptr;

/* 	jack_info( */
/* 		"connected: '%s' (%" PRIu64 "), of client %" PRIu64 " and '%s' (%" PRIu64 "), of client %" PRIu64, */
/* 		port1_ptr->name, */
/* 		port1_id, */
/* 		client1_id, */
/* 		port2_ptr->name, */
/* 		port2_id, */
/* 		client2_id); */

	connection_ptr = malloc(sizeof(struct jack_graph_connection));
	if (connection_ptr == NULL)
	{
		jack_error("Memory allocation of jack_graph_connection structure failed.");
		return NULL;
	}

	connection_ptr->id = connection_id;
	connection_ptr->port1 = port1_ptr;
	connection_ptr->port2 = port2_ptr;

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_add_tail(&connection_ptr->siblings, &patchbay_ptr->graph.connections);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_ports_connected(
		patchbay_ptr->graph.version,
		client1_id,
		port1_ptr->client->name,
		port1_id,
		port1_ptr->name,
		client2_id,
		port2_ptr->client->name,
		port2_id,
		port2_ptr->name,
		connection_id);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	return connection_ptr;
}

#define connection_ptr ((struct jack_graph_connection *)connection_context)

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
	void * connection_context)
{
/* 	jack_info( */
/* 		"disconnected: '%s' (%" PRIu64 "), of client %" PRIu64 " and '%s' (%" PRIu64 "), of client %" PRIu64, */
/* 		port1_ptr->name, */
/* 		port1_id, */
/* 		client1_id, */
/* 		port2_ptr->name, */
/* 		port2_id, */
/* 		client2_id); */

	if (connection_ptr == NULL)
	{
		jack_error("Ignoring removal of connection that failed to appear.");
		return;
	}

	pthread_mutex_lock(&patchbay_ptr->lock);
	list_del(&connection_ptr->siblings);
	patchbay_ptr->graph.version++;
	jack_controller_patchbay_send_signal_ports_disconnected(
		patchbay_ptr->graph.version,
		client1_id,
		port1_ptr->client->name,
		port1_id,
		port1_ptr->name,
		client2_id,
		port2_ptr->client->name,
		port2_id,
		port2_ptr->name,
		connection_id);
	jack_controller_patchbay_send_signal_graph_changed(patchbay_ptr->graph.version);
	pthread_mutex_unlock(&patchbay_ptr->lock);

	free(connection_ptr);
}

#undef connection_ptr
#undef port1_ptr
#undef port2_ptr
#undef patchbay_ptr

#define controller_ptr ((struct jack_controller *)call->context)
#define patchbay_ptr ((struct jack_controller_patchbay *)controller_ptr->patchbay_context)

static
void
jack_controller_dbus_get_all_ports(
	struct jack_dbus_method_call * call)
{
	struct list_head * client_node_ptr;
	struct list_head * port_node_ptr;
	struct jack_graph_client * client_ptr;
	struct jack_graph_port * port_ptr;
	DBusMessageIter iter, sub_iter;
	char fullname[JACK_CLIENT_NAME_SIZE + JACK_PORT_NAME_SIZE];
	char *fullname_var = fullname;

	call->reply = dbus_message_new_method_return (call->message);
	if (!call->reply)
	{
		goto fail;
	}

	dbus_message_iter_init_append (call->reply, &iter);

	if (!dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, "s", &sub_iter))
	{
		goto fail_unref;
	}

	pthread_mutex_lock(&patchbay_ptr->lock);

	list_for_each(client_node_ptr, &patchbay_ptr->graph.clients)
	{
		client_ptr = list_entry(client_node_ptr, struct jack_graph_client, siblings);

		list_for_each(port_node_ptr, &client_ptr->ports)
		{
			port_ptr = list_entry(port_node_ptr, struct jack_graph_port, siblings_client);

			jack_info("%s:%s", client_ptr->name, port_ptr->name);
			sprintf(fullname, "%s:%s", client_ptr->name, port_ptr->name);
			if (!dbus_message_iter_append_basic (&sub_iter, DBUS_TYPE_STRING, &fullname_var))
			{
				pthread_mutex_unlock(&patchbay_ptr->lock);
				dbus_message_iter_close_container (&iter, &sub_iter);
				goto fail_unref;
			}
		}
	}

	pthread_mutex_unlock(&patchbay_ptr->lock);

	if (!dbus_message_iter_close_container (&iter, &sub_iter))
	{
		goto fail_unref;
	}

	return;

fail_unref:
	dbus_message_unref (call->reply);
	call->reply = NULL;

fail:
	jack_error ("Ran out of memory trying to construct method return");
}

static
void
jack_controller_dbus_get_graph(
	struct jack_dbus_method_call * call)
{
	struct list_head * client_node_ptr;
	struct list_head * port_node_ptr;
	struct list_head * connection_node_ptr;
	struct jack_graph_client * client_ptr;
	struct jack_graph_port * port_ptr;
	struct jack_graph_connection * connection_ptr;
	DBusMessageIter iter;
	DBusMessageIter clients_array_iter;
	DBusMessageIter client_struct_iter;
	DBusMessageIter ports_array_iter;
	DBusMessageIter port_struct_iter;
	dbus_uint64_t version;
	DBusMessageIter connections_array_iter;
	DBusMessageIter connection_struct_iter;

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_UINT64, &version, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		goto exit;
	}

	//jack_info("Getting graph, know version is %" PRIu32, version);

	call->reply = dbus_message_new_method_return(call->message);
	if (!call->reply)
	{
		jack_error("Ran out of memory trying to construct method return");
		goto exit;
	}

	dbus_message_iter_init_append (call->reply, &iter);

	pthread_mutex_lock(&patchbay_ptr->lock);

	if (version > patchbay_ptr->graph.version)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_INVALID_ARGS,
			"known graph version %" PRIu64 " is newer than actual version %" PRIu64,
			version,
			patchbay_ptr->graph.version);
		pthread_mutex_unlock(&patchbay_ptr->lock);
		goto exit;
	}

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT64, &patchbay_ptr->graph.version))
	{
		goto nomem_unlock;
	}

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(tsa(tsuu))", &clients_array_iter))
	{
		goto nomem_unlock;
	}

	if (version < patchbay_ptr->graph.version)
	{
		list_for_each(client_node_ptr, &patchbay_ptr->graph.clients)
		{
			client_ptr = list_entry(client_node_ptr, struct jack_graph_client, siblings);

			if (!dbus_message_iter_open_container (&clients_array_iter, DBUS_TYPE_STRUCT, NULL, &client_struct_iter))
			{
				goto nomem_close_clients_array;
			}

			if (!dbus_message_iter_append_basic(&client_struct_iter, DBUS_TYPE_UINT64, &client_ptr->id))
			{
				goto nomem_close_client_struct;
			}

			if (!dbus_message_iter_append_basic(&client_struct_iter, DBUS_TYPE_STRING, &client_ptr->name))
			{
				goto nomem_close_client_struct;
			}

			if (!dbus_message_iter_open_container(&client_struct_iter, DBUS_TYPE_ARRAY, "(tsuu)", &ports_array_iter))
			{
				goto nomem_close_client_struct;
			}

			list_for_each(port_node_ptr, &client_ptr->ports)
			{
				port_ptr = list_entry(port_node_ptr, struct jack_graph_port, siblings_client);

				if (!dbus_message_iter_open_container(&ports_array_iter, DBUS_TYPE_STRUCT, NULL, &port_struct_iter))
				{
					goto nomem_close_ports_array;
				}

				if (!dbus_message_iter_append_basic(&port_struct_iter, DBUS_TYPE_UINT64, &port_ptr->id))
				{
					goto nomem_close_port_struct;
				}

				if (!dbus_message_iter_append_basic(&port_struct_iter, DBUS_TYPE_STRING, &port_ptr->name))
				{
					goto nomem_close_port_struct;
				}

				if (!dbus_message_iter_append_basic(&port_struct_iter, DBUS_TYPE_UINT32, &port_ptr->flags))
				{
					goto nomem_close_port_struct;
				}

				if (!dbus_message_iter_append_basic(&port_struct_iter, DBUS_TYPE_UINT32, &port_ptr->type))
				{
					goto nomem_close_port_struct;
				}

				if (!dbus_message_iter_close_container(&ports_array_iter, &port_struct_iter))
				{
					goto nomem_close_ports_array;
				}
			}

			if (!dbus_message_iter_close_container(&client_struct_iter, &ports_array_iter))
			{
				goto nomem_close_client_struct;
			}

			if (!dbus_message_iter_close_container(&clients_array_iter, &client_struct_iter))
			{
				goto nomem_close_clients_array;
			}
		}
	}

	if (!dbus_message_iter_close_container(&iter, &clients_array_iter))
	{
		goto nomem_unlock;
	}

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(tstststst)", &connections_array_iter))
	{
		goto nomem_unlock;
	}

	if (version < patchbay_ptr->graph.version)
	{
		list_for_each(connection_node_ptr, &patchbay_ptr->graph.connections)
		{
			connection_ptr = list_entry(connection_node_ptr, struct jack_graph_connection, siblings);

			if (!dbus_message_iter_open_container(&connections_array_iter, DBUS_TYPE_STRUCT, NULL, &connection_struct_iter))
			{
				goto nomem_close_connections_array;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_UINT64, &connection_ptr->port1->client->id))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_STRING, &connection_ptr->port1->client->name))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_UINT64, &connection_ptr->port1->id))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_STRING, &connection_ptr->port1->name))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_UINT64, &connection_ptr->port2->client->id))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_STRING, &connection_ptr->port2->client->name))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_UINT64, &connection_ptr->port2->id))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_STRING, &connection_ptr->port2->name))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_append_basic(&connection_struct_iter, DBUS_TYPE_UINT64, &connection_ptr->id))
			{
				goto nomem_close_connection_struct;
			}

			if (!dbus_message_iter_close_container(&connections_array_iter, &connection_struct_iter))
			{
				goto nomem_close_connections_array;
			}
		}
	}

	if (!dbus_message_iter_close_container(&iter, &connections_array_iter))
	{
		goto nomem_unlock;
	}

	pthread_mutex_unlock(&patchbay_ptr->lock);

	return;

nomem_close_connection_struct:
	dbus_message_iter_close_container(&connections_array_iter, &connection_struct_iter);

nomem_close_connections_array:
	dbus_message_iter_close_container(&iter, &connections_array_iter);
	goto nomem_unlock;

nomem_close_port_struct:
	dbus_message_iter_close_container(&ports_array_iter, &port_struct_iter);

nomem_close_ports_array:
	dbus_message_iter_close_container(&client_struct_iter, &ports_array_iter);

nomem_close_client_struct:
	dbus_message_iter_close_container(&clients_array_iter, &client_struct_iter);

nomem_close_clients_array:
	dbus_message_iter_close_container(&iter, &clients_array_iter);

nomem_unlock:
	pthread_mutex_unlock(&patchbay_ptr->lock);

//nomem:
	dbus_message_unref(call->reply);
	call->reply = NULL;
	jack_error("Ran out of memory trying to construct method return");

exit:
	return;
}

static
void
jack_controller_dbus_connect_ports_by_name(
	struct jack_dbus_method_call * call)
{
	const char * client1_name;
	const char * port1_name;
	const char * client2_name;
	const char * port2_name;

/* 	jack_info("jack_controller_dbus_connect_ports_by_name() called."); */

	if (!controller_ptr->started)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
			"Can't execute this method with stopped JACK server");
		return;
	}

	if (!jack_dbus_get_method_args(
		    call,
		    DBUS_TYPE_STRING,
		    &client1_name,
		    DBUS_TYPE_STRING,
		    &port1_name,
		    DBUS_TYPE_STRING,
		    &client2_name,
		    DBUS_TYPE_STRING,
		    &port2_name,
		    DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

/* 	jack_info("connecting %s:%s and %s:%s", client1_name, port1_name, client2_name, port2_name); */

	if (!jackctl_connect_ports_by_name(
		    controller_ptr->server,
		    client1_name,
		    port1_name,
		    client2_name,
		    port2_name))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_GENERIC,
			"jackctl_connect_ports_by_name() failed.");
		return;
	}

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_dbus_connect_ports_by_id(
	struct jack_dbus_method_call * call)
{
	dbus_uint64_t port1;
	dbus_uint64_t port2;

	jack_info("jack_controller_dbus_connect_ports_by_id() called.");

	if (!controller_ptr->started)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
			"Can't execute this method with stopped JACK server");
		return;
	}

	if (!jack_dbus_get_method_args(
		    call,
		    DBUS_TYPE_UINT64,
		    &port1,
		    DBUS_TYPE_UINT64,
		    &port2,
		    DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	if (!jackctl_connect_ports_by_id(
		    controller_ptr->server,
		    port1,
		    port2))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_GENERIC,
			"jackctl_connect_ports_by_id() failed.");
		return;
	}

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_dbus_disconnect_ports_by_name(
	struct jack_dbus_method_call * call)
{
	const char * client1_name;
	const char * port1_name;
	const char * client2_name;
	const char * port2_name;

/* 	jack_info("jack_controller_dbus_disconnect_ports_by_name() called."); */

	if (!controller_ptr->started)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
			"Can't execute this method with stopped JACK server");
		return;
	}

	if (!jack_dbus_get_method_args(
		    call,
		    DBUS_TYPE_STRING,
		    &client1_name,
		    DBUS_TYPE_STRING,
		    &port1_name,
		    DBUS_TYPE_STRING,
		    &client2_name,
		    DBUS_TYPE_STRING,
		    &port2_name,
		    DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

/* 	jack_info("disconnecting %s:%s and %s:%s", client1_name, port1_name, client2_name, port2_name); */

	if (!jackctl_disconnect_ports_by_name(
		    controller_ptr->server,
		    client1_name,
		    port1_name,
		    client2_name,
		    port2_name))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_GENERIC,
			"jackctl_disconnect_ports_by_name() failed.");
		return;
	}

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_dbus_disconnect_ports_by_id(
	struct jack_dbus_method_call * call)
{
	dbus_uint64_t port1;
	dbus_uint64_t port2;

	jack_info("jack_controller_dbus_disconnect_ports_by_id() called.");

	if (!controller_ptr->started)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
			"Can't execute this method with stopped JACK server");
		return;
	}

	if (!jack_dbus_get_method_args(
		    call,
		    DBUS_TYPE_UINT64,
		    &port1,
		    DBUS_TYPE_UINT64,
		    &port2,
		    DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	if (!jackctl_disconnect_ports_by_id(
		    controller_ptr->server,
		    port1,
		    port2))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_GENERIC,
			"jackctl_disconnect_ports_by_id() failed.");
		return;
	}

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_dbus_disconnect_ports_by_connection_id(
	struct jack_dbus_method_call * call)
{
	dbus_uint64_t id;

	jack_info("jack_controller_dbus_disconnect_ports_by_connection_id() called.");

	if (!controller_ptr->started)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_SERVER_NOT_RUNNING,
			"Can't execute this method with stopped JACK server");
		return;
	}

	if (!jack_dbus_get_method_args(
		    call,
		    DBUS_TYPE_UINT64,
		    &id,
		    DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	if (!jackctl_disconnect_ports_by_connection_id(
		    controller_ptr->server,
		    id))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_GENERIC,
			"jackctl_disconnect_ports_by_connection_id() failed.");
		return;
	}

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_dbus_get_client_pid(
    struct jack_dbus_method_call * call)
{
    dbus_uint64_t client_id;
    struct jack_graph_client *client_ptr;
    message_arg_t arg;

/*     jack_info("jack_controller_dbus_get_client_pid() called."); */

    if (!jack_dbus_get_method_args(
            call,
            DBUS_TYPE_UINT64,
            &client_id,
            DBUS_TYPE_INVALID))
    {
        /* The method call had invalid arguments meaning that
         * jack_dbus_get_method_args() has constructed an error for us.
         */
        return;
    }

    pthread_mutex_lock(&patchbay_ptr->lock);

    client_ptr = jack_controller_patchbay_find_client_by_id(patchbay_ptr, client_id);
    if (client_ptr == NULL)
    {
        jack_dbus_error(call, JACK_DBUS_ERROR_INVALID_ARGS, "cannot find client %" PRIu64, client_id);
        goto unlock;
    }

    arg.int64 = client_ptr->pid;

    jack_dbus_construct_method_return_single(call, DBUS_TYPE_INT64, arg);

unlock:
    pthread_mutex_unlock(&patchbay_ptr->lock);
}

#undef controller_ptr

void
jack_controller_patchbay_uninit(
	struct jack_controller * controller_ptr)
{
	//jack_info("jack_controller_patchbay_uninit() called");

	pthread_mutex_destroy(&patchbay_ptr->lock);
}

#undef patchbay_ptr

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetAllPorts)
	JACK_DBUS_METHOD_ARGUMENT("ports_list", "as", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetGraph)
	JACK_DBUS_METHOD_ARGUMENT("known_graph_version", DBUS_TYPE_UINT64_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("current_graph_version", DBUS_TYPE_UINT64_AS_STRING, true)
	JACK_DBUS_METHOD_ARGUMENT("clients_and_ports", "a(tsa(tsuu))", true)
	JACK_DBUS_METHOD_ARGUMENT("connections", "a(tstststst)", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(ConnectPortsByName)
	JACK_DBUS_METHOD_ARGUMENT("client1_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port1_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("client2_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port2_name", DBUS_TYPE_STRING_AS_STRING, false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(ConnectPortsByID)
	JACK_DBUS_METHOD_ARGUMENT("port1_id", DBUS_TYPE_UINT64_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port2_id", DBUS_TYPE_UINT64_AS_STRING, false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(DisconnectPortsByName)
	JACK_DBUS_METHOD_ARGUMENT("client1_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port1_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("client2_name", DBUS_TYPE_STRING_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port2_name", DBUS_TYPE_STRING_AS_STRING, false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(DisconnectPortsByID)
	JACK_DBUS_METHOD_ARGUMENT("port1_id", DBUS_TYPE_UINT64_AS_STRING, false)
	JACK_DBUS_METHOD_ARGUMENT("port2_id", DBUS_TYPE_UINT64_AS_STRING, false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(DisconnectPortsByConnectionID)
	JACK_DBUS_METHOD_ARGUMENT("connection_id", DBUS_TYPE_UINT64_AS_STRING, false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetClientPID)
    JACK_DBUS_METHOD_ARGUMENT("client_id", DBUS_TYPE_UINT64_AS_STRING, false)
    JACK_DBUS_METHOD_ARGUMENT("process_id", DBUS_TYPE_INT64_AS_STRING, true)
JACK_DBUS_METHOD_ARGUMENTS_END

static
const struct jack_dbus_interface_method_descriptor g_jack_controller_patchbay_iface_methods[] =
{
	JACK_DBUS_METHOD_DESCRIBE(GetAllPorts, jack_controller_dbus_get_all_ports)
	JACK_DBUS_METHOD_DESCRIBE(GetGraph, jack_controller_dbus_get_graph)
	JACK_DBUS_METHOD_DESCRIBE(ConnectPortsByName, jack_controller_dbus_connect_ports_by_name)
	JACK_DBUS_METHOD_DESCRIBE(ConnectPortsByID, jack_controller_dbus_connect_ports_by_id)
	JACK_DBUS_METHOD_DESCRIBE(DisconnectPortsByName, jack_controller_dbus_disconnect_ports_by_name)
	JACK_DBUS_METHOD_DESCRIBE(DisconnectPortsByID, jack_controller_dbus_disconnect_ports_by_id)
	JACK_DBUS_METHOD_DESCRIBE(DisconnectPortsByConnectionID, jack_controller_dbus_disconnect_ports_by_connection_id)
	JACK_DBUS_METHOD_DESCRIBE(GetClientPID, jack_controller_dbus_get_client_pid)
	JACK_DBUS_METHOD_DESCRIBE_END
};

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(GraphChanged)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(ClientAppeared)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_name", DBUS_TYPE_STRING_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(ClientDisappeared)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_name", DBUS_TYPE_STRING_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(PortAppeared)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_flags", DBUS_TYPE_UINT32_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_type", DBUS_TYPE_UINT32_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(PortDisappeared)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port_name", DBUS_TYPE_STRING_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(PortsConnected)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client1_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client1_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port1_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port1_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client2_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client2_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port2_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port2_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("connection_id", DBUS_TYPE_UINT64_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

JACK_DBUS_SIGNAL_ARGUMENTS_BEGIN(PortsDisconnected)
	JACK_DBUS_SIGNAL_ARGUMENT("new_graph_version", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client1_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client1_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port1_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port1_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client2_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("client2_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port2_id", DBUS_TYPE_UINT64_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("port2_name", DBUS_TYPE_STRING_AS_STRING)
	JACK_DBUS_SIGNAL_ARGUMENT("connection_id", DBUS_TYPE_UINT64_AS_STRING)
JACK_DBUS_SIGNAL_ARGUMENTS_END

static
const struct jack_dbus_interface_signal_descriptor g_jack_controller_patchbay_iface_signals[] =
{
	JACK_DBUS_SIGNAL_DESCRIBE(GraphChanged)
	JACK_DBUS_SIGNAL_DESCRIBE(ClientAppeared)
	JACK_DBUS_SIGNAL_DESCRIBE(ClientDisappeared)
	JACK_DBUS_SIGNAL_DESCRIBE(PortAppeared)
	JACK_DBUS_SIGNAL_DESCRIBE(PortDisappeared)
	JACK_DBUS_SIGNAL_DESCRIBE(PortsConnected)
	JACK_DBUS_SIGNAL_DESCRIBE(PortsDisconnected)
	JACK_DBUS_SIGNAL_DESCRIBE_END
};

JACK_DBUS_IFACE_DESCRIBE(
	g_jack_controller_iface_patchbay,
	JACK_DBUS_IFACE_NAME,
	g_jack_controller_patchbay_iface_methods,
	g_jack_controller_patchbay_iface_signals);
