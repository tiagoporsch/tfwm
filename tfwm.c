#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

// Config
static const struct shortcut {
	KeySym key;
	unsigned int mask;
	char* command;
} shortcuts[] = {
	{ XK_a, Mod4Mask, "dmenu_run" },
	{ XK_e, Mod4Mask, "st" },
	{ XK_f, Mod4Mask, "pcmanfm" },
	{ XK_w, Mod4Mask, "firefox" },
};
static const char* font = "Source Code Pro:style=bold:size=10";
static const char* colors[] = { "#000000", "#FFFFFF" };
static const int screen_width = 1920;
static const int screen_height = 1080;
static const int status_bar_height = 20;

// Global variables
static const int shortcut_count = sizeof(shortcuts) / sizeof(shortcuts[0]);
static const int color_count = sizeof(colors) / sizeof(colors[0]);
static const int desktop_width = screen_width;
static const int desktop_height = screen_height - status_bar_height;

Display* display;
Window root;
XftDraw* xft_draw;
XftFont* xft_font;
XftColor* xft_colors;

// Status bar
char status[128] = "No status script";

void draw_status_bar() {
	XClearWindow(display, root);
	XftDrawStringUtf8(xft_draw, &xft_colors[1], xft_font,
		screen_width - strlen(status) * xft_font->max_advance_width,
		screen_height - 6, (const FcChar8*) status, strlen(status));
}

// Helpers
void spawn(char* cmd) {
	if (fork())
		return;
	setsid();
	execlp(cmd, cmd, NULL);
}

void despawn(Window window) {
	const Atom wm_protocols = XInternAtom(display, "WM_PROTOCOLS", false);
	const Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", false);

	Atom* protocols;
	int protocol_count;
	if (XGetWMProtocols(display, window, &protocols, &protocol_count)) {
		for (int i = 0; i < protocol_count; i++) {
			if (protocols[protocol_count] == wm_delete_window) {
				XEvent event;
				event.xclient.type = ClientMessage;
				event.xclient.window = window;
				event.xclient.message_type = wm_protocols;
				event.xclient.format = 32;
				event.xclient.data.l[0] = wm_delete_window;
				XSendEvent(display, window, false, NoEventMask, &event);
				XFree(protocols);
				return;
			}
		}
		XFree(protocols);
	}

	XKillClient(display, window);
}

// Keys and buttons
int mod[] = { 0, LockMask, 0, LockMask };

void grab_key(int key, int mask) {
	for (int i = 0; i < 4; i++) {
		XGrabKey(display, XKeysymToKeycode(display, key), mod[i] | mask, root,
			true, true, true);
	}
}

void grab_button(int button, int mask) {
	for (int i = 0; i < 4; i++) {
		XGrabButton(display, button, mod[i] | mask, root, True, ButtonPressMask,
			GrabModeAsync, GrabModeAsync, None, None);
	}
}

// Main
int main() {
	display = XOpenDisplay(0);
	if (!display)
		return 1;
	root = DefaultRootWindow(display);
	XSelectInput(display, root, EnterWindowMask | SubstructureNotifyMask
		| SubstructureRedirectMask | PropertyChangeMask);

	// Colors
	Visual* visual = DefaultVisual(display, DefaultScreen(display));
	Colormap colormap = DefaultColormap(display, DefaultScreen(display));
	xft_draw = XftDrawCreate(display, root, visual, colormap);
	xft_font = XftFontOpenName(display, DefaultScreen(display), font);
	xft_colors = malloc(color_count * sizeof(XftColor));
	for (int i = 0; i < color_count; i++)
		XftColorAllocName(display, visual, colormap, colors[i], &xft_colors[i]);
	XSetWindowBackground(display, root, xft_colors[0].pixel);

	// Get numlock mask
	XModifierKeymap* mk = XGetModifierMapping(display);
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < mk->max_keypermod; j++) {
			if (mk->modifiermap[i * mk->max_keypermod + j]
					== XKeysymToKeycode(display, XK_Num_Lock)) {
				mod[2] |= 1 << i;
				mod[3] |= 1 << i;
			}
		}
	}
	XFreeModifiermap(mk);

	// Grabs
	grab_button(1, Mod4Mask); // Drag
	grab_button(3, Mod4Mask); // Resize
	grab_key(XK_q, Mod4Mask | ShiftMask); // Quit
	grab_key(XK_q, Mod4Mask); // Close window
	grab_key(XK_Tab, Mod4Mask); // Lower window
	for (unsigned i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++)
		grab_key(shortcuts[i].key, shortcuts[i].mask);

	// Main loop
	XEvent ev;
	XWindowAttributes attr;
	XButtonEvent start;
	bool quit = false;
	while (!quit) {
		XNextEvent(display, &ev);
		if (ev.type == EnterNotify) {
			Window window = ev.xcrossing.window;
			XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
		} else if (ev.type == MapRequest) {
			Window window = ev.xmaprequest.window;
			XSelectInput(display, window, EnterWindowMask);
			XMapWindow(display, window);
			XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
			XMoveResizeWindow(display, ev.xmaprequest.window,
				screen_width / 4, desktop_height / 4,
				screen_width / 2, desktop_height / 2);
		} else if (ev.type == PropertyNotify) {
			if (ev.xproperty.atom == XA_WM_NAME && ev.xproperty.window == root) {
				XTextProperty prop;
				if (XGetTextProperty(display, root, &prop, XA_WM_NAME)) {
					if (prop.encoding == XA_STRING) {
						strncpy(status, (char*) prop.value, sizeof(status) - 1);
					}
					XFree(prop.value);
					draw_status_bar();
				}
			}
		} else if (ev.type == KeyPress) {
			KeySym key = XKeycodeToKeysym(display, ev.xkey.keycode, 0);
			unsigned int mask = ev.xkey.state & (Mod4Mask | ShiftMask);
			if (key == XK_q && mask == (Mod4Mask | ShiftMask)) {
				quit = true;
			} else if (key == XK_q && mask == Mod4Mask) {
				if (ev.xkey.subwindow != None) {
					despawn(ev.xkey.subwindow);
				}
			} else if (key == XK_Tab && mask == Mod4Mask) {
				if (ev.xkey.subwindow != None) {
					XLowerWindow(display, ev.xkey.subwindow);
				}
			} else for (int i = 0; i < shortcut_count; i++) {
				struct shortcut shortcut = shortcuts[i];
				if (key == shortcut.key && mask == shortcut.mask) {
					spawn(shortcut.command);
				}
			}
		} else if (ev.type == ButtonPress) {
			if (ev.xbutton.subwindow != None) {
				XRaiseWindow(display, ev.xbutton.subwindow);
				XGrabPointer(display, ev.xbutton.subwindow, True,
					PointerMotionMask | ButtonReleaseMask, GrabModeAsync,
					GrabModeAsync, None, None, CurrentTime);
				XGetWindowAttributes(display, ev.xbutton.subwindow, &attr);
				start = ev.xbutton;
			}
		} else if (ev.type == MotionNotify) {
			while (XCheckTypedEvent(display, MotionNotify, &ev));
			int mx = ev.xbutton.x_root;
			int my = ev.xbutton.y_root;
			if (start.button == 1) {
				if (mx == 0 && my == 0) {
					XMoveResizeWindow(display, ev.xmotion.window,
						0, 0,
						desktop_width / 2, desktop_height / 2);
				} else if (mx == screen_width - 1 && my == 0) {
					XMoveResizeWindow(display, ev.xmotion.window,
						desktop_width / 2, 0,
						desktop_width / 2, desktop_height / 2);
				} else if (mx == screen_width - 1 && my == screen_height - 1) {
					XMoveResizeWindow(display, ev.xmotion.window,
						desktop_width / 2, desktop_height / 2,
						desktop_width / 2, desktop_height / 2);
				} else if (mx == 0 && my == screen_height - 1) {
					XMoveResizeWindow(display, ev.xmotion.window,
						0, desktop_height / 2,
						desktop_width / 2, desktop_height / 2);
				} else if (my == 0) {
					XMoveResizeWindow(display, ev.xmotion.window,
						0, 0,
						desktop_width, desktop_height);
				} else if (mx == 0) {
					XMoveResizeWindow(display, ev.xmotion.window,
						0, 0,
						desktop_width / 2, desktop_height);
				} else if (mx == screen_width - 1) {
					XMoveResizeWindow(display, ev.xmotion.window,
						desktop_width / 2, 0,
						desktop_width / 2, desktop_height);
				} else {
					XMoveResizeWindow(display, ev.xmotion.window,
						attr.x + mx - start.x_root, attr.y + my - start.y_root,
						attr.width, attr.height);
				}
			} else if (start.button == 3) {
				int width = attr.width + mx - start.x_root;
				int height = attr.height + my - start.y_root;
				if (width < 16) width = 16;
				if (height < 16) height = 16;
				XMoveResizeWindow(display, ev.xmotion.window,
					attr.x, attr.y, width, height);
			}
		} else if (ev.type == ButtonRelease) {
			XUngrabPointer(display, CurrentTime);
		}
	}

	free(xft_colors);
	XCloseDisplay(display);
	return 0;
}
