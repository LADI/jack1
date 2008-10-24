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
#include <assert.h>
#include <dbus/dbus.h>

#include <jack/internal.h>

#include "jackdbus.h"
#include "jackcontroller_internal.h"
#include "jackcontroller_xml.h"

unsigned char jack_controller_dbus_types[JACK_PARAM_MAX] =
{
	[JackParamInt] = DBUS_TYPE_INT32,
	[JackParamUInt] = DBUS_TYPE_UINT32,
	[JackParamChar] = DBUS_TYPE_BYTE,
	[JackParamString] = DBUS_TYPE_STRING,
	[JackParamBool] = DBUS_TYPE_BOOLEAN,
};

const char * jack_controller_dbus_type_signatures[JACK_PARAM_MAX] =
{
	[JackParamInt] = DBUS_TYPE_INT32_AS_STRING,
	[JackParamUInt] = DBUS_TYPE_UINT32_AS_STRING,
	[JackParamChar] = DBUS_TYPE_BYTE_AS_STRING,
	[JackParamString] = DBUS_TYPE_STRING_AS_STRING,
	[JackParamBool] = DBUS_TYPE_BOOLEAN_AS_STRING,
};

#define PARAM_TYPE_JACK_TO_DBUS(_) jack_controller_dbus_types[_]
#define PARAM_TYPE_JACK_TO_DBUS_SIGNATURE(_) jack_controller_dbus_type_signatures[_]

static
bool
jack_controller_jack_to_dbus_variant(
	jackctl_param_type_t type,
	const union jackctl_parameter_value * value_ptr,
	message_arg_t  * dbusv_ptr)
{
	switch (type)
	{
	case JackParamInt:
		dbusv_ptr->int32 = (dbus_int32_t)value_ptr->i;
		return true;
	case JackParamUInt:
		dbusv_ptr->uint32 = (dbus_uint32_t)value_ptr->ui;
		return true;
	case JackParamChar:
		dbusv_ptr->byte = value_ptr->c;
		return true;
	case JackParamString:
		dbusv_ptr->string = value_ptr->str;
		return true;
	case JackParamBool:
		dbusv_ptr->boolean = (dbus_bool_t)value_ptr->b;
		return true;
	}

	jack_error("Unknown JACK parameter type %i", (int)type);
	assert(0);
	return false;
}

static
bool
jack_controller_dbus_to_jack_variant(
	int type,
	const message_arg_t * dbusv_ptr,
	union jackctl_parameter_value * value_ptr)
{
	size_t len;

	switch (type)
	{
	case DBUS_TYPE_INT32:
		value_ptr->i = dbusv_ptr->int32;
		return true;
	case DBUS_TYPE_UINT32:
		value_ptr->ui = dbusv_ptr->uint32;
		return true;
	case DBUS_TYPE_BYTE:
		value_ptr->c = dbusv_ptr->byte;
		return true;
	case DBUS_TYPE_STRING:
		len = strlen(dbusv_ptr->string);
		if (len > JACK_PARAM_STRING_MAX)
		{
			jack_error("Parameter string value is too long (%u)", (unsigned int)len);
			return false;
		}
		memcpy(value_ptr->str, dbusv_ptr->string, len + 1);

		return true;
	case DBUS_TYPE_BOOLEAN:
		value_ptr->b = dbusv_ptr->boolean;
		return true;
	}

	jack_error("Unknown D-Bus parameter type %i", (int)type);
	return false;
}

/*
 * Construct a return message for a Get[Driver|Engine]ParameterValue method call.
 *
 * The operation can only fail due to lack of memory, in which case
 * there's no sense in trying to construct an error return. Instead,
 * call->reply will be set to NULL and handled in send_method_return().
 */
static void
jack_dbus_construct_method_return_parameter(
	struct jack_dbus_method_call * call,
	dbus_bool_t is_set,
	int type,
	const char * signature,
	message_arg_t default_value,
	message_arg_t value)
{
	DBusMessageIter iter;

	/* Create a new method return message. */
	call->reply = dbus_message_new_method_return (call->message);
	if (!call->reply)
	{
		goto fail;
	}

	dbus_message_iter_init_append (call->reply, &iter);

	/* Append the is_set argument. */
	if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, (const void *) &is_set))
	{
		goto fail_unref;
	}

	/* Append the 'default' and 'value' arguments. */
	if (!jack_dbus_message_append_variant(&iter, type, signature, &default_value))
	{
		goto fail_unref;
	}
	if (!jack_dbus_message_append_variant(&iter, type, signature, &value))
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

#define controller_ptr ((struct jack_controller *)call->context)

static
void
jack_controller_dbus_get_available_drivers(
	struct jack_dbus_method_call * call)
{
	jack_dbus_construct_method_return_array_of_strings(
		call,
		controller_ptr->drivers_count,
		controller_ptr->driver_names);
}

static
void
jack_controller_dbus_get_selected_driver(
	struct jack_dbus_method_call * call)
{
	message_arg_t arg;

	if (controller_ptr->driver != NULL)
	{
		arg.string = jackctl_driver_get_name(controller_ptr->driver);
	}
	else
	{
		arg.string = NULL;
	}

	jack_dbus_construct_method_return_single(call, DBUS_TYPE_STRING, arg);
}

static
void
jack_controller_dbus_select_driver(
	struct jack_dbus_method_call * call)
{
	const char * driver_name;

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_STRING, &driver_name, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	if (!jack_controller_select_driver(controller_ptr, driver_name))
	{
		/* Couldn't find driver with the specified name. */
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_DRIVER,
			"Unknown driver \"%s\"",
			driver_name);
		return;
	}

	jack_controller_settings_save_auto(controller_ptr);

	jack_dbus_construct_method_return_empty(call);
}

static
void
jack_controller_get_parameters_info(
	struct jack_dbus_method_call * call,
	const JSList * parameters_list)
{
	DBusMessageIter iter, array_iter, struct_iter;
	unsigned char type;
	const char *str;

	call->reply = dbus_message_new_method_return (call->message);
	if (!call->reply)
	{
		goto fail;
	}

	dbus_message_iter_init_append (call->reply, &iter);

	/* Open the array. */
	if (!dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, "(ysss)", &array_iter))
	{
		goto fail_unref;
	}

	/* Append parameter descriptions to the array. */
	while (parameters_list != NULL)
	{
		/* Open the struct. */
		if (!dbus_message_iter_open_container (&array_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter))
		{
			goto fail_close_unref;
		}

		/* Append parameter type. */
		type = PARAM_TYPE_JACK_TO_DBUS(jackctl_parameter_get_type((jackctl_parameter)parameters_list->data));
		if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BYTE,
		                                     (const void *) &type))
		{
			goto fail_close2_unref;
		}

		/* Append parameter name. */
		str = jackctl_parameter_get_name((jackctl_parameter)parameters_list->data);
		if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
		                                     (const void *) &str))
		{
			goto fail_close2_unref;
		}

		/* Append parameter short description. */
		str = jackctl_parameter_get_short_description((jackctl_parameter)parameters_list->data);
		if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
		                                     (const void *) &str))
		{
			goto fail_close2_unref;
		}

		/* Append parameter long description. */
		str = jackctl_parameter_get_long_description((jackctl_parameter)parameters_list->data);
		if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
		                                     (const void *) &str))
		{
			goto fail_close2_unref;
		}

		/* Close the struct. */
		if (!dbus_message_iter_close_container (&array_iter, &struct_iter))
		{
			goto fail_close_unref;
		}

		parameters_list = jack_slist_next(parameters_list);
	}

	/* Close the array. */
	if (!dbus_message_iter_close_container (&iter, &array_iter))
	{
		goto fail_unref;
	}

	return;

fail_close2_unref:
	dbus_message_iter_close_container (&iter, &struct_iter);

fail_close_unref:
	dbus_message_iter_close_container (&iter, &array_iter);

fail_unref:
	dbus_message_unref (call->reply);
	call->reply = NULL;

fail:
	jack_error ("Ran out of memory trying to construct method return");
}

static
void
jack_controller_get_parameter_info(
	struct jack_dbus_method_call * call,
	jackctl_parameter parameter)
{
	DBusMessageIter iter, struct_iter;
	unsigned char type;
	const char *str;

	call->reply = dbus_message_new_method_return (call->message);
	if (!call->reply)
	{
		goto fail;
	}

	dbus_message_iter_init_append (call->reply, &iter);

	/* Open the struct. */
	if (!dbus_message_iter_open_container (&iter, DBUS_TYPE_STRUCT, NULL, &struct_iter))
	{
		goto fail_unref;
	}

	/* Append parameter type. */
	type = PARAM_TYPE_JACK_TO_DBUS(jackctl_parameter_get_type(parameter));
	if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BYTE,
					     (const void *) &type))
	{
		goto fail_close_unref;
	}

	/* Append parameter name. */
	str = jackctl_parameter_get_name(parameter);
	if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
					     (const void *) &str))
	{
		goto fail_close_unref;
	}

	/* Append parameter short description. */
	str = jackctl_parameter_get_short_description(parameter);
	if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
					     (const void *) &str))
	{
		goto fail_close_unref;
	}

	/* Append parameter long description. */
	str = jackctl_parameter_get_long_description(parameter);
	if (!dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING,
					     (const void *) &str))
	{
		goto fail_close_unref;
	}

	/* Close the struct. */
	if (!dbus_message_iter_close_container (&iter, &struct_iter))
	{
		goto fail_unref;
	}

	return;

fail_close_unref:
	dbus_message_iter_close_container (&iter, &struct_iter);

fail_unref:
	dbus_message_unref (call->reply);
	call->reply = NULL;

fail:
	jack_error ("Ran out of memory trying to construct method return");
}

/*
 * Execute GetDriverParametersInfo method call.
 */
static
void
jack_controller_dbus_get_driver_parameters_info(
	struct jack_dbus_method_call * call)
{
	if (controller_ptr->driver == NULL)
	{
		jack_dbus_error (call, JACK_DBUS_ERROR_NEED_DRIVER,
				 "No driver selected");
		return;
	}

	jack_controller_get_parameters_info(
		call,
		jackctl_driver_get_parameters(controller_ptr->driver));
}

/*
 * Execute GetDriverParameterInfo method call.
 */
static
void
jack_controller_dbus_get_driver_parameter_info(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	jackctl_parameter parameter;

	if (controller_ptr->driver == NULL)
	{
		jack_dbus_error (call, JACK_DBUS_ERROR_NEED_DRIVER,
				 "No driver selected");
		return;
	}

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_STRING, &parameter_name, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_driver_get_parameters(controller_ptr->driver), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_DRIVER_PARAMETER,
			"Unknown parameter \"%s\" for driver \"%s\"",
			parameter,
			jackctl_driver_get_name(controller_ptr->driver));
		return;
	}

	jack_controller_get_parameter_info(call, parameter);
}


/*
 * Execute GetDriverParameterValue method call.
 */
static void
jack_controller_dbus_get_driver_parameter_value(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	jackctl_parameter parameter;
	int type;
	union jackctl_parameter_value jackctl_value;
	union jackctl_parameter_value jackctl_default_value;
	message_arg_t value;
	message_arg_t default_value;

	if (controller_ptr->driver == NULL)
	{
		jack_dbus_error (call, JACK_DBUS_ERROR_NEED_DRIVER,
				 "No driver selected");
		return;
	}

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_STRING, &parameter_name, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * get_method_args() has constructed an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_driver_get_parameters(controller_ptr->driver), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_DRIVER_PARAMETER,
			"Unknown parameter \"%s\" for driver \"%s\"",
			parameter,
			jackctl_driver_get_name(controller_ptr->driver));
		return;
	}
	
	type = jackctl_parameter_get_type(parameter);
	jackctl_default_value = jackctl_parameter_get_default_value(parameter);
	jackctl_value = jackctl_parameter_get_value(parameter);

	jack_controller_jack_to_dbus_variant(type, &jackctl_value, &value);
	jack_controller_jack_to_dbus_variant(type, &jackctl_default_value, &default_value);

	/* Construct the reply. */
	jack_dbus_construct_method_return_parameter(
		call,
		(dbus_bool_t)(jackctl_parameter_is_set(parameter) ? TRUE : FALSE),
		PARAM_TYPE_JACK_TO_DBUS(type),
		PARAM_TYPE_JACK_TO_DBUS_SIGNATURE(type),
		default_value,
		value);
}

static
void
jack_controller_dbus_set_driver_parameter_value(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	message_arg_t arg;
	int arg_type;
	jackctl_parameter parameter;
	jackctl_param_type_t type;
	union jackctl_parameter_value value;

	if (controller_ptr->driver == NULL)
	{
		jack_dbus_error (call, JACK_DBUS_ERROR_NEED_DRIVER,
				 "No driver selected");
		return;
	}

	if (!jack_dbus_get_method_args_string_and_variant(call, &parameter_name, &arg, &arg_type))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args_string_and_variant() has constructed
		 * an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_driver_get_parameters(controller_ptr->driver), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_DRIVER_PARAMETER,
			"Unknown parameter \"%s\" for driver \"%s\"",
			parameter,
			jackctl_driver_get_name(controller_ptr->driver));
		return;
	}

	type = jackctl_parameter_get_type(parameter);

	if (PARAM_TYPE_JACK_TO_DBUS(type) != arg_type)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_INVALID_ARGS,
			"Engine parameter value type mismatch: was expecting '%c', got '%c'",
			(char)PARAM_TYPE_JACK_TO_DBUS(type),
			(char)arg_type);
		return;
	}

	if (!jack_controller_dbus_to_jack_variant(
		    arg_type,
		    &arg,
		    &value))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_INVALID_ARGS,
			"Cannot convert engine parameter value");
		return;
	}

	jackctl_parameter_set_value(parameter, &value);

	jack_controller_settings_save_auto(controller_ptr);

	jack_dbus_construct_method_return_empty(call);
}

/*
 * Execute GetEngineParametersInfo method call.
 */
static
void
jack_controller_dbus_get_engine_parameters_info(
	struct jack_dbus_method_call * call)
{
	jack_controller_get_parameters_info(
		call,
		jackctl_server_get_parameters(controller_ptr->server));
}

/*
 * Execute GetEngineParameterInfo method call.
 */
static
void
jack_controller_dbus_get_engine_parameter_info(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	jackctl_parameter parameter;

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_STRING, &parameter_name, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_server_get_parameters(controller_ptr->server), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_ENGINE_PARAMETER,
			"Unknown engine parameter \"%s\"",
			parameter);
		return;
	}

	jack_controller_get_parameter_info(call, parameter);
}

/*
 * Execute GetDriverParameterValue method call.
 */
static
void
jack_controller_dbus_get_engine_parameter_value(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	jackctl_parameter parameter;
	jackctl_param_type_t type;
	union jackctl_parameter_value jackctl_value;
	union jackctl_parameter_value jackctl_default_value;
	message_arg_t value;
	message_arg_t default_value;

	if (!jack_dbus_get_method_args(call, DBUS_TYPE_STRING, &parameter_name, DBUS_TYPE_INVALID))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args() has constructed an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_server_get_parameters(controller_ptr->server), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_ENGINE_PARAMETER,
			"Unknown engine parameter \"%s\"",
			parameter);
		return;
	}

	type = jackctl_parameter_get_type(parameter);
	jackctl_default_value = jackctl_parameter_get_default_value(parameter);
	jackctl_value = jackctl_parameter_get_value(parameter);

	jack_controller_jack_to_dbus_variant(type, &jackctl_value, &value);
	jack_controller_jack_to_dbus_variant(type, &jackctl_default_value, &default_value);

	/* Construct the reply. */
	jack_dbus_construct_method_return_parameter(
		call,
		(dbus_bool_t)(jackctl_parameter_is_set(parameter) ? TRUE : FALSE),
		PARAM_TYPE_JACK_TO_DBUS(type),
		PARAM_TYPE_JACK_TO_DBUS_SIGNATURE(type),
		default_value,
		value);
}

static
void
jack_controller_dbus_set_engine_parameter_value(
	struct jack_dbus_method_call * call)
{
	const char * parameter_name;
	message_arg_t arg;
	int arg_type;
	jackctl_parameter parameter;
	jackctl_param_type_t type;
	union jackctl_parameter_value value;

	if (!jack_dbus_get_method_args_string_and_variant (call, &parameter_name, &arg, &arg_type))
	{
		/* The method call had invalid arguments meaning that
		 * jack_dbus_get_method_args_string_and_variant() has constructed
		 * an error for us.
		 */
		return;
	}

	parameter = jack_controller_find_parameter(jackctl_server_get_parameters(controller_ptr->server), parameter_name);
	if (parameter == NULL)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_UNKNOWN_ENGINE_PARAMETER,
			"Unknown engine parameter \"%s\"",
			parameter);
		return;
	}

	type = jackctl_parameter_get_type(parameter);

	if (PARAM_TYPE_JACK_TO_DBUS(type) != arg_type)
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_INVALID_ARGS,
			"Engine parameter value type mismatch: was expecting '%c', got '%c'",
			(char)PARAM_TYPE_JACK_TO_DBUS(type),
			(char)arg_type);
		return;
	}

	if (!jack_controller_dbus_to_jack_variant(
		    arg_type,
		    &arg,
		    &value))
	{
		jack_dbus_error(
			call,
			JACK_DBUS_ERROR_INVALID_ARGS,
			"Cannot convert engine parameter value");
		return;
	}

	jackctl_parameter_set_value(parameter, &value);

	jack_controller_settings_save_auto(controller_ptr);

	jack_dbus_construct_method_return_empty(call);
}

#undef controller_ptr

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetAvailableDrivers)
	JACK_DBUS_METHOD_ARGUMENT("drivers_list", "as", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetSelectedDriver)
	JACK_DBUS_METHOD_ARGUMENT("driver", "s", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(SelectDriver)
	JACK_DBUS_METHOD_ARGUMENT("driver", "s", false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetDriverParametersInfo)
	JACK_DBUS_METHOD_ARGUMENT("parameter_info_array", "a(ysss)", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetDriverParameterInfo)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("parameter_info", "(ysss)", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetDriverParameterValue)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("is_set", "b", true)
	JACK_DBUS_METHOD_ARGUMENT("default", "v", true)
	JACK_DBUS_METHOD_ARGUMENT("value", "v", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(SetDriverParameterValue)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("value", "v", false)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetEngineParametersInfo)
	JACK_DBUS_METHOD_ARGUMENT("parameter_info_array", "a(ysss)", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetEngineParameterInfo)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("parameter_info", "(ysss)", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(GetEngineParameterValue)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("is_set", "b", true)
	JACK_DBUS_METHOD_ARGUMENT("default", "v", true)
	JACK_DBUS_METHOD_ARGUMENT("value", "v", true)
JACK_DBUS_METHOD_ARGUMENTS_END

JACK_DBUS_METHOD_ARGUMENTS_BEGIN(SetEngineParameterValue)
	JACK_DBUS_METHOD_ARGUMENT("parameter", "s", false)
	JACK_DBUS_METHOD_ARGUMENT("value", "v", false)
JACK_DBUS_METHOD_ARGUMENTS_END

static
const struct jack_dbus_interface_method_descriptor g_jack_controller_configure_iface_methods[] =
{
	JACK_DBUS_METHOD_DESCRIBE(GetAvailableDrivers, jack_controller_dbus_get_available_drivers)
	JACK_DBUS_METHOD_DESCRIBE(GetSelectedDriver, jack_controller_dbus_get_selected_driver)
	JACK_DBUS_METHOD_DESCRIBE(SelectDriver, jack_controller_dbus_select_driver)
	JACK_DBUS_METHOD_DESCRIBE(GetDriverParametersInfo, jack_controller_dbus_get_driver_parameters_info)
	JACK_DBUS_METHOD_DESCRIBE(GetDriverParameterInfo, jack_controller_dbus_get_driver_parameter_info)
	JACK_DBUS_METHOD_DESCRIBE(GetDriverParameterValue, jack_controller_dbus_get_driver_parameter_value)
	JACK_DBUS_METHOD_DESCRIBE(SetDriverParameterValue, jack_controller_dbus_set_driver_parameter_value)
	JACK_DBUS_METHOD_DESCRIBE(GetEngineParametersInfo, jack_controller_dbus_get_engine_parameters_info)
	JACK_DBUS_METHOD_DESCRIBE(GetEngineParameterInfo, jack_controller_dbus_get_engine_parameter_info)
	JACK_DBUS_METHOD_DESCRIBE(GetEngineParameterValue, jack_controller_dbus_get_engine_parameter_value)
	JACK_DBUS_METHOD_DESCRIBE(SetEngineParameterValue, jack_controller_dbus_set_engine_parameter_value)
	JACK_DBUS_METHOD_DESCRIBE_END
};

JACK_DBUS_IFACE_DESCRIBE(
	g_jack_controller_iface_configure,
	"org.jackaudio.JackConfigure",
	g_jack_controller_configure_iface_methods,
	NULL);
