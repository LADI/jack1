/*
    Copyright (C) 2003 Bob Ham <rah@bash.sh>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#ifndef __jack_driver_interface_h__
#define __jack_driver_interface_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>

#include <jack/jack.h>
#include <jack/internal.h>

#define JACK_DRIVER_NAME_MAX          15
#define JACK_DRIVER_PARAM_NAME_MAX    15
#define JACK_DRIVER_PARAM_STRING_MAX  63


/** Driver parameter types */
typedef enum
{
  JackDriverParamInt = 1,
  JackDriverParamUInt,
  JackDriverParamChar,
  JackDriverParamString,
  JackDriverParamBool

} jack_driver_param_type_t;

/** Driver parameter value */
typedef union
{
  uint32_t  ui;
  int32_t   i;
  char      c;
  char      str[JACK_DRIVER_PARAM_STRING_MAX+1];
} jack_driver_param_value_t;


/** A driver parameter descriptor */
typedef struct
{
  char name[JACK_DRIVER_NAME_MAX+1]; /**< The parameter's name */
  char character;                    /**< The parameter's character (for getopt, etc) */
  jack_driver_param_type_t type;     /**< The parameter's type */
  jack_driver_param_value_t value;   /**< The parameter's (default) value */
  char short_desc[64];               /**< A short (~30 chars) description for the user */
  char long_desc[1024];              /**< A longer description for the user */

} jack_driver_param_desc_t;

/** A driver parameter */
typedef struct
{
  char character;
  jack_driver_param_value_t value;
} jack_driver_param_t;


/** A struct for describing a jack driver */
typedef struct
{
  char name[JACK_DRIVER_NAME_MAX+1];        /**< The driver's canonical name */
  char file[PATH_MAX+1];                    /**< The filename of the driver's shared object file */
  uint32_t nparams;                         /**< The number of parameters the driver has */
  jack_driver_param_desc_t * params;        /**< An array of parameter descriptors */
  
} jack_driver_desc_t;




#ifdef __cplusplus
}
#endif

#endif /* __jack_driver_interface_h__ */


