/*
 * X11DRV desktop window handling
 *
 * Copyright 2001 Alexandre Julliard
 * Copyright 2020 Zhiyi Zhang for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <X11/cursorfont.h>
#include <X11/Xlib.h>

#include "x11drv.h"

/* avoid conflict with field names in included win32 headers */
#undef Status
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11drv);

static RECT host_primary_rect;

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1

/* Return TRUE if Wine is currently in virtual desktop mode */
BOOL is_virtual_desktop(void)
{
    return root_window != DefaultRootWindow( gdi_display );
}

/***********************************************************************
 *		X11DRV_init_desktop
 *
 * Setup the desktop when not using the root window.
 */
void X11DRV_init_desktop( Window win, unsigned int width, unsigned int height )
{
    host_primary_rect = get_host_primary_monitor_rect();
    root_window = win;
    managed_mode = FALSE;  /* no managed windows in desktop mode */
}

/***********************************************************************
 *           X11DRV_CreateDesktop
 *
 * Create the X11 desktop window for the desktop mode.
 */
BOOL X11DRV_CreateDesktop( const WCHAR *name, UINT width, UINT height )
{
    XSetWindowAttributes win_attr;
    Window win;
    Display *display = thread_init_display();

    TRACE( "%s %ux%u\n", debugstr_w(name), width, height );

    /* Create window */
    win_attr.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | EnterWindowMask |
                          PointerMotionMask | ButtonPressMask | ButtonReleaseMask | FocusChangeMask;
    win_attr.cursor = XCreateFontCursor( display, XC_top_left_arrow );

    if (default_visual.visual != DefaultVisual( display, DefaultScreen(display) ))
        win_attr.colormap = XCreateColormap( display, DefaultRootWindow(display),
                                             default_visual.visual, AllocNone );
    else
        win_attr.colormap = None;

    win = XCreateWindow( display, DefaultRootWindow(display),
                         0, 0, width, height, 0, default_visual.depth, InputOutput,
                         default_visual.visual, CWEventMask | CWCursor | CWColormap, &win_attr );
    if (!win) return FALSE;
    XFlush( display );

    X11DRV_init_desktop( win, width, height );
    return TRUE;
}

BOOL is_desktop_fullscreen(void)
{
    RECT primary_rect = NtUserGetPrimaryMonitorRect();
    return (primary_rect.right - primary_rect.left == host_primary_rect.right - host_primary_rect.left &&
            primary_rect.bottom - primary_rect.top == host_primary_rect.bottom - host_primary_rect.top);
}

static void update_desktop_fullscreen( unsigned int width, unsigned int height)
{
    Display *display = thread_display();
    XEvent xev;

    if (!display || !is_virtual_desktop()) return;

    xev.xclient.type = ClientMessage;
    xev.xclient.window = DefaultRootWindow(display);
    xev.xclient.message_type = x11drv_atom(_NET_WM_STATE);
    xev.xclient.serial = 0;
    xev.xclient.display = display;
    xev.xclient.send_event = True;
    xev.xclient.format = 32;
    if (width == host_primary_rect.right - host_primary_rect.left && height == host_primary_rect.bottom - host_primary_rect.top)
        xev.xclient.data.l[0] = _NET_WM_STATE_ADD;
    else
        xev.xclient.data.l[0] = _NET_WM_STATE_REMOVE;
    xev.xclient.data.l[1] = x11drv_atom(_NET_WM_STATE_FULLSCREEN);
    xev.xclient.data.l[2] = 0;
    xev.xclient.data.l[3] = 1;

    TRACE("action=%li\n", xev.xclient.data.l[0]);

    XSendEvent( display, DefaultRootWindow(display), False,
                SubstructureRedirectMask | SubstructureNotifyMask, &xev );

    xev.xclient.data.l[1] = x11drv_atom(_NET_WM_STATE_MAXIMIZED_VERT);
    xev.xclient.data.l[2] = x11drv_atom(_NET_WM_STATE_MAXIMIZED_HORZ);
    XSendEvent( display, DefaultRootWindow(display), False,
                SubstructureRedirectMask | SubstructureNotifyMask, &xev );
}

/***********************************************************************
 *		X11DRV_resize_desktop
 */
void X11DRV_resize_desktop(void)
{
    static RECT old_virtual_rect;

    RECT primary_rect, virtual_rect;
    HWND hwnd = NtUserGetDesktopWindow();
    INT width, height;

    virtual_rect = NtUserGetVirtualScreenRect();
    primary_rect = NtUserGetPrimaryMonitorRect();
    width = primary_rect.right;
    height = primary_rect.bottom;

    TRACE( "desktop %p change to (%dx%d)\n", hwnd, width, height );
    update_desktop_fullscreen( width, height );
    NtUserSetWindowPos( hwnd, 0, virtual_rect.left, virtual_rect.top,
                        virtual_rect.right - virtual_rect.left, virtual_rect.bottom - virtual_rect.top,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_DEFERERASE );

    if (old_virtual_rect.left != virtual_rect.left || old_virtual_rect.top != virtual_rect.top)
        send_message_timeout( HWND_BROADCAST, WM_X11DRV_DESKTOP_RESIZED, old_virtual_rect.left,
                              old_virtual_rect.top, SMTO_ABORTIFHUNG, 2000, FALSE );

    old_virtual_rect = virtual_rect;
}
