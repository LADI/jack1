=======================================================================
*** README.developers - JACK development practices                  ***
=======================================================================

:Formatting: restructured text, http://docutils.sourceforge.net/rst.html

What is this? 
-----------------------------------------------------------------------

This file is a collection of practices and rules for JACK
development under LADI project.

For LADI specific issues, submit issues or pull request to LADI project.
For related discussions, you are invited to join
`Libera.Chat <https://libera.chat/>`_ channel #ladi

Contents
-----------------------------------------------------------------------

- What is this?
- Important files for developers
- Version numbers
- Decision Process


Important files for developers
-----------------------------------------------------------------------

AUTHORS
	List of contributors. If you have contributed code, mail Paul 
	Davis to get your name added to the list, or if you have 
	GIT-access, help yourself. :) Also remember to update the
	per source file copyright statements when committing changes.

README.developers 
	This file.

TODO
	A one file mini-bugzilla for JACK developers. Note: this file
	is no longer actively updated - use the github issue tracker instead.

libjack/ChangeLog
	A list of _all_ changes to the public interface! Note: this file
	is not updated since 2005 - use the git history instead.


Version numbers 
-----------------------------------------------------------------------

JACK's package version
~~~~~~~~~~~~~~~~~~~~~~

JACK's package version is set in configure.in, and consists of 
major, minor and revision numbers. This version should be 
updated whenever a non-trivial set of changes is committed 
to GIT and packaged downstream:
 
major version
   ask in #ladi :)

minor version
   incremented when any of the public or internal
   interfaces are changed

revision
   incremented when implementation-only
   changes are made

Client API versioning
~~~~~~~~~~~~~~~~~~~~~

JACK clients are affected by two interfaces, the JACK Client API (libjack)
and the JACK Client Protocol API (interface between jackd and 
libjack). The former one is versioned using libtool interface 
versioniong (set in configure.in). This version should be 
updated whenever a non-trivial set of changes is committed 
to GIT and packaged downstream:

current
    incremented whenever the public libjack API is changed 
   
revision
    incremented when the libjack implementation is changed
    
age
    current libjack is both source and binary compatible with
    libjack interfaces current,current-1,...,current-age

Note! It was decided by original jackaudio authors in January 2003
      that current interface number
      will remain as zero until the first stable JACK version
      is released.
      In LADI jack1 version line, the first stable version is 1.121.4

JACK Client Protocol is versioned... <TBD>.

Note! All changes that affect the libjack API must be documented 
in jack/libjack/ChangeLog using the standard ChangeLog style
(see GNU developer docs).


Sending patches
---------------------------------------------------------------------

Uses of external libraries and other packages
-----------------------------------------------------------------------

The main JACK components, jackd and libjack, should only use 
standard POSIX and ANSI-C services. If use of other interfaces is
absolutely needed, it should be made optional in the build process (via
a configure switch for example). 

Other components like example clients and drivers, may rely on other 
packages, but these dependencies should not affect the main build 
process.
