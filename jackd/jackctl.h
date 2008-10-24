/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    JACK control API

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

#ifndef JACKCTL_H__2EEDAD78_DF4C_4B26_83B7_4FF1A446A47E__INCLUDED
#define JACKCTL_H__2EEDAD78_DF4C_4B26_83B7_4FF1A446A47E__INCLUDED

/** Parameter types, intentionally similar to jack_driver_param_type_t */
typedef enum
{
  JackParamInt = 1,
  JackParamUInt,
  JackParamChar,
  JackParamString,
  JackParamBool,
} jackctl_param_type_t;

#define JACK_PARAM_MAX (JackParamBool + 1)

/** max length of string parameter value, excluding terminating nul char */
#define JACK_PARAM_STRING_MAX  63

/** Parameter value, intentionally similar to jack_driver_param_value_t */
union jackctl_parameter_value
{
  uint32_t ui;
  int32_t i;
  char c;
  char str[JACK_PARAM_STRING_MAX + 1];
  bool b;
};

/** handle to server object */
typedef struct { int unused; } * jackctl_server;

/** handle to driver object */
typedef struct { int unused; } * jackctl_driver;

/** handle to parameter object */
typedef struct { int unused; } * jackctl_parameter;

typedef
void *
(* jackctl_client_appeared_callback)(
	void * server_context,
	uint64_t client_id,
	const char * client_name);

typedef
void
(* jackctl_client_disappeared_callback)(
	void * server_context,
	uint64_t client_id,
	void * client_context);

typedef
void *
(* jackctl_port_appeared_callback)(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	const char * port_name,
	uint32_t port_flags,
	uint32_t port_type);

typedef
void
(* jackctl_port_disappeared_callback)(
	void * server_context,
	uint64_t client_id,
	void * client_context,
	uint64_t port_id,
	void * port_context);

typedef
void *
(* jackctl_ports_connected_callback)(
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

typedef
void
(* jackctl_ports_disconnected_callback)(
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

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* Adjust editor indent */
#endif

/**
 * Create server object
 *
 */
jackctl_server jackctl_server_create(const char * name);
void jackctl_server_destroy(jackctl_server server);

const JSList * jackctl_server_get_drivers_list(jackctl_server server);

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
	jackctl_ports_disconnected_callback ports_disconnected_callback);

bool jackctl_server_stop(jackctl_server server);

double jackctl_server_get_load(jackctl_server server);
unsigned int jackctl_server_get_sample_rate(jackctl_server server);
double jackctl_server_get_latency(jackctl_server server);
unsigned int jackctl_server_get_buffer_size(jackctl_server server);
bool jackctl_server_set_buffer_size(jackctl_server server, unsigned int nframes);
bool jackctl_server_is_realtime(jackctl_server server);
unsigned int jackctl_server_get_xruns(jackctl_server server);
void jackctl_server_reset_xruns(jackctl_server server);

const JSList * jackctl_server_get_parameters(jackctl_server server);

const char * jackctl_driver_get_name(jackctl_driver driver);

const JSList * jackctl_driver_get_parameters(jackctl_driver driver);

const char * jackctl_parameter_get_name(jackctl_parameter parameter);
const char * jackctl_parameter_get_short_description(jackctl_parameter parameter);
const char * jackctl_parameter_get_long_description(jackctl_parameter parameter);
jackctl_param_type_t jackctl_parameter_get_type(jackctl_parameter parameter);
bool jackctl_parameter_is_set(jackctl_parameter parameter);

union jackctl_parameter_value jackctl_parameter_get_value(jackctl_parameter parameter);
bool jackctl_parameter_set_value(jackctl_parameter parameter, const union jackctl_parameter_value * value_ptr);
union jackctl_parameter_value jackctl_parameter_get_default_value(jackctl_parameter parameter);

bool
jackctl_connect_ports_by_name(
	jackctl_server server,
	const char * client1_name,
	const char * port1_name,
	const char * client2_name,
	const char * port2_name);

bool
jackctl_connect_ports_by_id(
	jackctl_server server,
	const char * port1_id,
	const char * port2_id);

bool
jackctl_disconnect_ports_by_name(
	jackctl_server server,
	const char * client1_name,
	const char * port1_name,
	const char * client2_name,
	const char * port2_name);

bool
jackctl_disconnect_ports_by_id(
	jackctl_server server,
	const char * port1_id,
	const char * port2_id);

bool
jackctl_disconnect_ports_by_connection_id(
	jackctl_server server,
	const char * connection_id);

#if 0
{ /* Adjust editor indent */
#endif
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* #ifndef JACKCTL_H__2EEDAD78_DF4C_4B26_83B7_4FF1A446A47E__INCLUDED */
