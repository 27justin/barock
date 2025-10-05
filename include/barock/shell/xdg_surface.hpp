#include "wl/xdg-shell-protocol.h"

/*
  version 7
  desktop user interface surface base interface

  An interface that may be implemented by a wl_surface, for implementations that provide a
  desktop-style user interface.

  It provides a base set of functionality required to construct user interface elements requiring
  management by the compositor, such as toplevel windows, menus, etc. The types of functionality are
  split into xdg_surface roles.

  Creating an xdg_surface does not set the role for a wl_surface. In order to map an xdg_surface,
  the client must create a role-specific object using, e.g., get_toplevel, get_popup. The wl_surface
  for any given xdg_surface can have at most one role, and may not be assigned any role not based on
  xdg_surface.

  A role must be assigned before any other requests are made to the xdg_surface object.

  The client must call wl_surface.commit on the corresponding wl_surface for the xdg_surface state
  to take effect.

  Creating an xdg_surface from a wl_surface which has a buffer attached or committed is a client
  error, and any attempts by a client to attach or manipulate a buffer prior to the first
  xdg_surface.configure call must also be treated as errors.

  After creating a role-specific object and setting it up (e.g. by sending the title, app ID, size
  constraints, parent, etc), the client must perform an initial commit without any buffer attached.
  The compositor will reply with initial wl_surface state such as wl_surface.preferred_buffer_scale
  followed by an xdg_surface.configure event. The client must acknowledge it and is then allowed to
  attach a buffer to map the surface.

  Mapping an xdg_surface-based role surface is defined as making it possible for the surface to be
  shown by the compositor. Note that a mapped surface is not guaranteed to be visible once it is
  mapped.

  For an xdg_surface to be mapped by the compositor, the following conditions must be met: (1) the
  client has assigned an xdg_surface-based role to the surface (2) the client has set and committed
  the xdg_surface state and the role-dependent state to the surface (3) the client has committed a
  buffer to the surface

  A newly-unmapped surface is considered to have met condition (1) out of the 3 required conditions
  for mapping a surface if its role surface has not been destroyed, i.e. the client must perform the
  initial commit again before attaching a buffer.
*/

extern struct xdg_surface_interface xdg_surface_impl;
