#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

// Configuration
static const struct shortcut {
	KeySym key;
	unsigned int mask;
	const char *command;
} shortcuts[] = {
	{ XK_a, Mod4Mask,           "rofi -show drun" },
	{ XK_e, Mod4Mask,           "st" },
	{ XK_s, Mod4Mask,           "maim -u | xclip -selection clipboard -t image/png" },
	{ XK_s, Mod4Mask|ShiftMask, "maim -s -u | xclip -selection clipboard -t image/png" },

	{ XK_Tab, Mod4Mask,           "!lower" },
	{ XK_Tab, Mod1Mask,           "!change" },
	{ XK_q,   Mod4Mask,           "!close" },
	{ XK_w,   Mod4Mask,           "!hide" },
	{ XK_q,   Mod4Mask|ShiftMask, "!quit" },
};
static const size_t shortcut_count = sizeof(shortcuts) / sizeof(shortcuts[0]);

enum {
	COLOR_TASKBAR,
	COLOR_TASKBAR_TEXT,
	COLOR_TASKBAR_FOCUS,
	COLOR_BORDER,
	COLOR_BORDER_FOCUS,
};
static const int colors[] = {
	[COLOR_TASKBAR] =		0x111111,
	[COLOR_TASKBAR_TEXT] =	0x7f7f7f,
	[COLOR_TASKBAR_FOCUS] =	0xffffff,
	[COLOR_BORDER] =		0x000000,
	[COLOR_BORDER_FOCUS] =	0x000000,
};

static const char* font = "monospace:style=bold:size=10";
static const int bar_height = 22;
static const int border_width = 1;

// Definitions
#define CLIENT_MAX_NAME_LENGHT 15

typedef enum anchor {
	ANCHOR_NONE, ANCHOR_TOP,
	ANCHOR_TOP_LEFT, ANCHOR_TOP_RIGHT,
	ANCHOR_LEFT, ANCHOR_RIGHT,
	ANCHOR_BOT_LEFT, ANCHOR_BOT_RIGHT,
} anchor_t;

typedef struct client {
	Window window;
	char name[CLIENT_MAX_NAME_LENGHT + 1];
	bool hidden;
	int x, y, w, h;
	int px, py, pw, ph;
	anchor_t anchor;
	struct client *next;
} client_t;

// Global variables
int screen_width;
int screen_height;
int view_height;

Display* display;
GC gc;
Window root;
Window bar;

XftDraw* xft_draw;
XftFont* xft_font;

client_t* clients = NULL;
client_t* focused = NULL;

// Atoms
Atom _NET_ACTIVE_WINDOW;
Atom _NET_CLIENT_LIST;
Atom _NET_DESKTOP_GEOMETRY;
Atom _NET_NUMBER_OF_DESKTOPS;
Atom _NET_WM_STATE;
Atom _NET_WM_STATE_DEMANDS_ATTENTION;
Atom _NET_WM_STATE_HIDDEN;
Atom _NET_WM_STATE_MAXIMIZED_VERT;
Atom _NET_WM_STATE_MAXIMIZED_HORZ;
Atom _NET_WM_WINDOW_TYPE;
Atom _NET_WM_WINDOW_TYPE_TOOLBAR;
Atom WM_CHANGE_STATE;
Atom WM_DELETE_WINDOW;
Atom WM_PROTOCOLS;
Atom WM_STATE;

void atom_init(void) {
	_NET_ACTIVE_WINDOW = XInternAtom(display, "_NET_ACTIVE_WINDOW", false);
	_NET_CLIENT_LIST = XInternAtom(display, "_NET_CLIENT_LIST", false);
	_NET_DESKTOP_GEOMETRY = XInternAtom(display, "_NET_DESKTOP_GEOMETRY", false);
	_NET_NUMBER_OF_DESKTOPS = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", false);
	_NET_WM_STATE = XInternAtom(display, "_NET_WM_STATE", false);
	_NET_WM_STATE_DEMANDS_ATTENTION = XInternAtom(display, "_NET_WM_STATE_DEMANDS_ATTENTION", false);
	_NET_WM_STATE_HIDDEN = XInternAtom(display, "_NET_WM_STATE_HIDDEN", false);
	_NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", false);
	_NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", false);
	_NET_WM_WINDOW_TYPE = XInternAtom(display, "_NET_WM_WINDOW_TYPE", false);
	_NET_WM_WINDOW_TYPE_TOOLBAR = XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR", false);
	WM_CHANGE_STATE = XInternAtom(display, "WM_CHANGE_STATE", false);
	WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", false);
	WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", false);
	WM_STATE = XInternAtom(display, "WM_STATE", false);
}

// Bar
char bar_status[128] = { 0 };

void bar_draw(void) {
	static const XftColor normal = { .color = {
		.alpha = 0xFFFF,
		.red = ((colors[COLOR_TASKBAR_TEXT] >> 16) & 0xFF) * 257,
		.green = ((colors[COLOR_TASKBAR_TEXT] >> 8) & 0xFF) * 257,
		.blue = (colors[COLOR_TASKBAR_TEXT] & 0xFF) * 257,
	}};
	static const XftColor focus = { .color = {
		.alpha = 0xFFFF,
		.red = ((colors[COLOR_TASKBAR_FOCUS] >> 16) & 0xFF) * 257,
		.green = ((colors[COLOR_TASKBAR_FOCUS] >> 8) & 0xFF) * 257,
		.blue = (colors[COLOR_TASKBAR_FOCUS] & 0xFF) * 257,
	}};

	// Clear background
	XSetForeground(display, gc, colors[COLOR_TASKBAR]);
	XFillRectangle(display, bar, gc, 0, 0, screen_width, bar_height);

	// Draw client buttons
	int x = 4;
	for (client_t* c = clients; c; c = c->next) {
		const XftColor *color = c == focused ? &focus : &normal;
		XftDrawStringUtf8(xft_draw, color, xft_font,
			x, bar_height - 6, (XftChar8*) "[", 1
		);
		x += xft_font->max_advance_width;
		XftDrawStringUtf8(xft_draw, color, xft_font,
			x, bar_height - 6, (XftChar8*) c->name, strlen(c->name)
		);
		x += strlen(c->name) * xft_font->max_advance_width;
		XftDrawStringUtf8(xft_draw, color, xft_font,
			x, bar_height - 6, (XftChar8*) "]", 1
		);
		x += xft_font->max_advance_width * 2;
	}

	// Draw status text
	XftDrawStringUtf8(xft_draw, &focus, xft_font,
		screen_width - strlen(bar_status) * xft_font->max_advance_width, bar_height - 6,
		(XftChar8*) bar_status, strlen(bar_status)
	);
}

// Client
client_t* client_find(Window window) {
	for (client_t* c = clients; c; c = c->next)
		if (c->window == window)
			return c;
	return NULL;
}

bool client_any_visible(void) {
	for (client_t *c = clients; c; c = c->next)
		if (!c->hidden)
			return true;
	return false;
}

void client_close(client_t* c) {
	Atom* protocols;
	int protocol_count;
	if (XGetWMProtocols(display, c->window, &protocols, &protocol_count)) {
		for (int i = 0; i < protocol_count; i++) {
			if (protocols[i] != WM_DELETE_WINDOW)
				continue;
			XEvent event;
			event.type = ClientMessage;
			event.xclient.window = c->window;
			event.xclient.message_type = WM_PROTOCOLS;
			event.xclient.format = 32;
			event.xclient.data.l[0] = WM_DELETE_WINDOW;
			event.xclient.data.l[1] = CurrentTime;
			XSendEvent(display, c->window, false, NoEventMask, &event);
			XFree(protocols);
			return;
		}
		XFree(protocols);
	}

	XGrabServer(display);
	XSetCloseDownMode(display, DestroyAll);
	XKillClient(display, c->window);
	XSync(display, false);
	XUngrabServer(display);
}

void client_move(client_t* c, int x, int y) {
	XMoveWindow(display, c->window, x, y);
	c->x = x;
	c->y = y;
}

void client_resize(client_t *c, int w, int h) {
	XResizeWindow(display, c->window, w, h);
	c->w = w;
	c->h = h;
}

void client_move_resize(client_t* c, int x, int y, int w, int h) {
	XMoveResizeWindow(display, c->window, x, y, w, h);
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
}

void client_update_state(client_t* c) {
	XDeleteProperty(display, c->window, _NET_WM_STATE);
	if (c->hidden)
		XChangeProperty(display, c->window, _NET_WM_STATE, XA_ATOM, 32, PropModeAppend, (unsigned char*) &_NET_WM_STATE_HIDDEN, 1);
	if (c->anchor == ANCHOR_TOP) {
		XChangeProperty(display, c->window, _NET_WM_STATE, XA_ATOM, 32, PropModeAppend, (unsigned char*) &_NET_WM_STATE_MAXIMIZED_VERT, 1);
		XChangeProperty(display, c->window, _NET_WM_STATE, XA_ATOM, 32, PropModeAppend, (unsigned char*) &_NET_WM_STATE_MAXIMIZED_HORZ, 1);
	}

	long state = c->hidden ? IconicState : NormalState;
	XChangeProperty(display, c->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*) &state, 1);
}

void client_hide(client_t* c) {
	if (c->hidden) return;
	c->hidden = true;
	XMoveWindow(display, c->window, -2 * c->w, c->y);
	client_update_state(c);
}

void client_show(client_t* c) {
	if (!c->hidden) return;
	c->hidden = false;
	XMoveWindow(display, c->window, c->x, c->y);
	client_update_state(c);
}

void client_anchor(client_t* c, anchor_t anchor) {
	if (c->anchor == anchor) return;
	if (c->anchor == ANCHOR_NONE) {
		c->px = c->x;
		c->py = c->y;
		c->pw = c->w;
		c->ph = c->h;
	}
	switch (anchor) {
		case ANCHOR_NONE: client_move_resize(c, c->px, c->py, c->pw, c->ph); break;
		case ANCHOR_TOP: client_move_resize(c, -border_width, bar_height, screen_width + 2*border_width, view_height + 2*border_width); break;
		case ANCHOR_TOP_LEFT: client_move_resize(c, -border_width, bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_TOP_RIGHT: client_move_resize(c, screen_width / 2, bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_LEFT: client_move_resize(c, -border_width, bar_height, screen_width / 2, view_height); break;
		case ANCHOR_RIGHT: client_move_resize(c, screen_width / 2, bar_height, screen_width / 2, view_height); break;
		case ANCHOR_BOT_LEFT: client_move_resize(c, -border_width, view_height / 2 + bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_BOT_RIGHT: client_move_resize(c, screen_width / 2, view_height / 2 + bar_height, screen_width / 2, view_height / 2); break;
	}
	c->anchor = anchor;
	client_update_state(c);
}

void client_focus(client_t* c) {
	if (focused)
		XSetWindowBorder(display, focused->window, colors[COLOR_BORDER]);
	if (c)
		XSetWindowBorder(display, c->window, colors[COLOR_BORDER_FOCUS]);
	focused = c;
	XSetInputFocus(display, c ? c->window : root, RevertToPointerRoot, CurrentTime);
	Window none = None;
	XChangeProperty(display, root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char*) (c ? &c->window : &none), 1);
	bar_draw();
}

void client_lower(client_t* c) {
	XLowerWindow(display, c->window);
	XLowerWindow(display, bar);
}

void client_raise(client_t* c) {
	if (c->hidden) client_show(c);
	XRaiseWindow(display, c->window);
	client_focus(c);
}

// Window
void window_get_title(Window window, char* buffer, size_t size) {
	XTextProperty prop;
	if (!XGetTextProperty(display, window, &prop, XA_WM_NAME))
		return;
	if (prop.encoding == XA_STRING) {
		strncpy(buffer, (char*) prop.value, size - 1);
		if (strlen((char*) prop.value) >= size - 1)
			strcpy(buffer + size - 4, "...");
	} else {
		char** list = NULL;
		int count = 0;
		int ret = XmbTextPropertyToTextList(display, &prop, &list, &count);
		if (ret >= Success && count > 0 && list[0] != NULL) {
			strncpy(buffer, list[0], size - 1);
			if (strlen(list[0]) >= size - 1)
				strcpy(buffer + size - 4, "...");
			XFreeStringList(list);
		}
	}
	XFree(prop.value);
}

// Event handlers
XWindowAttributes grab_attr;
XButtonEvent grab_start;

void handle_button_press(XButtonEvent* e) {
	if (e->window == bar) {
		int x = 4;
		for (client_t* c = clients; c; c = c->next) {
			int width = (strlen(c->name) + 2) * xft_font->max_advance_width;
			if (e->x_root - x <= width) {
				if (e->button == 1)
					client_raise(c);
				else if (e->button == 3)
					client_close(c);
				break;
			}
			x += width + xft_font->max_advance_width;
		}
	} else {
		client_t* c = client_find(e->subwindow);
		if (c == NULL)
			return;
		XRaiseWindow(display, c->window);
		XGrabPointer(display, c->window, false, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		XGetWindowAttributes(display, c->window, &grab_attr);
		grab_start = *e;
	}
}

void handle_button_release(XButtonEvent* e) {
	client_t* c = client_find(e->subwindow);
	if (c) client_focus(c);
	XUngrabPointer(display, CurrentTime);
	XSync(display, false);
}

void handle_client_message(XClientMessageEvent* e) {
	client_t* c = client_find(e->window);
	if (!c) return;

	if (e->message_type == _NET_ACTIVE_WINDOW || e->message_type == _NET_WM_STATE_DEMANDS_ATTENTION) {
		client_raise(c);
	} else if (e->message_type == _NET_WM_STATE) {
		if (e->data.l[1] == (long) _NET_WM_STATE_MAXIMIZED_VERT && e->data.l[2] == (long) _NET_WM_STATE_MAXIMIZED_HORZ) {
			switch (e->data.l[0]) {
				case 0: client_anchor(c, ANCHOR_NONE); break;
				case 1: client_anchor(c, ANCHOR_TOP); break;
				case 2: client_anchor(c, c->anchor == ANCHOR_NONE ? ANCHOR_TOP : ANCHOR_NONE); break;
			}
		} else if (e->data.l[1] == (long) _NET_WM_STATE_HIDDEN) {
			switch (e->data.l[0]) {
				case 0: client_show(c); break;
				case 1: client_hide(c); break;
				case 2: c->hidden ? client_show(c) : client_hide(c); break;
			}
		} else {
			char* n1 = e->data.l[1] ? XGetAtomName(display, e->data.l[1]) : NULL;
			char* n2 = e->data.l[2] ? XGetAtomName(display, e->data.l[2]) : NULL;
			fprintf(stderr, "unimplemented _NET_WM_STATE: %ld %s %s\n", e->data.l[0], n1 ? n1 : "", n2 ? n2 : "");
			if (n1) XFree(n1);
			if (n2) XFree(n2);
		}
	} else if (e->message_type == WM_CHANGE_STATE) {
		switch (e->data.l[0]) {
			case NormalState: client_show(c); break;
			case IconicState: client_hide(c); break;
		}
	} else {
		char* n = XGetAtomName(display, e->message_type);
		fprintf(stderr, "unimplemented Client Message: %s\n", n);
		if (n) XFree(n);
	}
}

void handle_configure_request(XConfigureRequestEvent* e) {
	client_t* c = client_find(e->window);
	if (!c) return;

	c->hidden = false;
	c->x = e->x;
	c->y = e->y;
	c->w = e->width;
	c->h = e->height;

	XWindowChanges changes;
	changes.x = e->x;
	changes.y = e->y;
	changes.width = e->width;
	changes.height = e->height;
	changes.border_width = e->border_width;
	changes.sibling = e->above;
	changes.stack_mode = e->detail;
	XConfigureWindow(display, e->window, e->value_mask, &changes);
	XSync(display, false);
}

void handle_enter_notify(XCrossingEvent* e) {
	if (e->window == bar)
		return;
	if (e->window == root && client_any_visible())
		return;
	client_focus(client_find(e->window));
}

void handle_expose(XExposeEvent* e) {
	if (e->window == bar)
		bar_draw();
	else if (e->window == root)
		XClearArea(display, root, e->x, e->y, e->width, e->height, false);
}

bool handle_key_press(XKeyEvent* e) {
	KeySym key = XkbKeycodeToKeysym(display, e->keycode, 0, 0);
	unsigned int mask = e->state & (Mod1Mask | Mod4Mask | ShiftMask);
	for (size_t i = 0; i < shortcut_count; i++) {
		struct shortcut shortcut = shortcuts[i];
		if (key != shortcut.key || mask != shortcut.mask)
			continue;
		if (shortcut.command[0] != '!') {
			if (fork())
				return false;
			if (display)
				close(ConnectionNumber(display));
			setsid();
			const char* command[4] = { "/bin/sh", "-c", shortcut.command, NULL };
			execvp((char*) command[0], (char**) command);
			exit(EXIT_SUCCESS);
		} else if (!strcmp(shortcut.command, "!change")) {
			client_t* c = focused ? focused : clients;
			if (c) client_raise(c->next ? c->next : clients);
		} else if (!strcmp(shortcut.command, "!close")) {
			client_t* c = client_find(e->subwindow);
			if (c) client_close(c);
		} else if (!strcmp(shortcut.command, "!hide")) {
			client_t* c = client_find(e->subwindow);
			if (c) client_hide(c);
		} else if (!strcmp(shortcut.command, "!lower")) {
			client_t* c = client_find(e->subwindow);
			if (c) client_lower(c);
		} else if (!strcmp(shortcut.command, "!quit")) {
			return true;
		}
	}
	return false;
}

void handle_map_notify(XMapEvent *e) {
	client_t *c = client_find(e->window);
	if (!c) return;
	client_raise(c);
}

void handle_map_request(XMapRequestEvent* e) {
	client_t* c = malloc(sizeof(*c));
	c->window = e->window;
	c->anchor = ANCHOR_NONE;
	c->hidden = true;
	c->next = clients;
	clients = c;

	XWindowAttributes attr;
	XGetWindowAttributes(display, e->window, &attr);

	int w;
	if (attr.width > screen_width) w = screen_width;
	else if (attr.width < 16) w = screen_width * 3 / 4;
	else w = attr.width;

	int h;
	if (attr.height > view_height) h = view_height;
	else if (attr.height < 16) h = (view_height) * 3 / 4;
	else h = attr.height;

	int x;
	if (attr.x <= 0) x = (screen_width - w) / 2;
	else x = attr.x;

	int y;
	if (attr.y <= 0) y = (view_height - h) / 2 + bar_height;
	else if (attr.y < bar_height) y = bar_height;
	else y = attr.y;

	window_get_title(c->window, c->name, sizeof(c->name));
	client_move_resize(c, x, y, w, h);

	XAddToSaveSet(display, c->window);
	XSelectInput(display, c->window, EnterWindowMask | PropertyChangeMask);
	XChangeProperty(display, root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*) &c->window, 1);
	XConfigureWindow(display, c->window, CWBorderWidth, &(XWindowChanges){.border_width=border_width});
	XSetWindowBorder(display, c->window, colors[COLOR_BORDER_FOCUS]);
	XMapWindow(display, c->window);
}

void handle_motion_notify(XMotionEvent* e) {
	client_t* c = client_find(e->window);
	if (!c) return;

	int mx = e->x_root;
	int my = e->y_root;
	if (grab_start.button == 1) {
		if (mx == 0 && my == 0)
			client_anchor(c, ANCHOR_TOP_LEFT);
		else if (mx == screen_width - 1 && my == 0)
			client_anchor(c, ANCHOR_TOP_RIGHT);
		else if (mx == 0 && my == screen_height - 1)
			client_anchor(c, ANCHOR_BOT_LEFT);
		else if (mx == screen_width - 1 && my == screen_height - 1)
			client_anchor(c, ANCHOR_BOT_RIGHT);
		else if (my == 0)
			client_anchor(c, ANCHOR_TOP);
		else if (mx == 0)
			client_anchor(c, ANCHOR_LEFT);
		else if (mx == screen_width - 1)
			client_anchor(c, ANCHOR_RIGHT);
		else {
			if (c->anchor != ANCHOR_NONE) {
				client_anchor(c, ANCHOR_NONE);
				grab_attr.x = grab_start.x_root - c->w / 2;
				grab_attr.y = grab_start.y_root - c->h / 2;
			}
			client_move(c, grab_attr.x + mx - grab_start.x_root, grab_attr.y + my - grab_start.y_root);
		}
	} else if (grab_start.button == 3 && c->anchor == ANCHOR_NONE) {
		int width = grab_attr.width + mx - grab_start.x_root;
		int height = grab_attr.height + my - grab_start.y_root;
		if (width < 64) width = 64;
		if (height < 32) height = 32;
		client_resize(c, width, height);
	}
}

void handle_property_notify(XPropertyEvent* e) {
	if (e->atom != XA_WM_NAME)
		return;
	if (e->window == root) {
		window_get_title(e->window, bar_status, sizeof(bar_status));
	} else {
		client_t* c = client_find(e->window);
		if (!c) return;
		window_get_title(c->window, c->name, sizeof(c->name));
	}
	bar_draw();
}

void handle_unmap_notify(XUnmapEvent* e) {
	for (client_t** c = &clients; *c; c = &(*c)->next) {
		if ((*c)->window != e->window)
			continue;
		client_t* t = *c;
		*c = (*c)->next;
		free(t);
		break;
	}

	XDeleteProperty(display, root, _NET_CLIENT_LIST);
	for (client_t* c = clients; c; c = c->next)
		XChangeProperty(display, root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*) &c->window, 1);

	bar_draw();
}

// Main
int error_event_handler(Display* d, XErrorEvent* e) {
	char msg[256];
	XGetErrorText(d, e->error_code, msg, sizeof(msg));
	fprintf(stderr, "tfwm: %s (request %d)\n", msg, e->request_code);
	return 0;
}

int fatal_error_event_handler(Display* d, XErrorEvent* e) {
	char msg[256];
	XGetErrorText(d, e->error_code, msg, sizeof(msg));
	fprintf(stderr, "tfwm: %s (request %d)\n", msg, e->request_code);
	exit(EXIT_FAILURE);
}

int main(void) {
	// Manage zombie processes
	signal(SIGCHLD, SIG_IGN);

	// Connect to the X server
	display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "tfwm: error opening display\n");
		exit(EXIT_FAILURE);
	}
	XSetErrorHandler(fatal_error_event_handler);
	XSelectInput(display, DefaultRootWindow(display), SubstructureRedirectMask);
	XSync(display, false);
	XSetErrorHandler(error_event_handler);
	XSync(display, false);
	screen_width = DisplayWidth(display, DefaultScreen(display));
	screen_height = DisplayHeight(display, DefaultScreen(display));
	view_height = screen_height - bar_height;
	root = DefaultRootWindow(display);
	gc = XCreateGC(display, root, 0, 0);
	XSelectInput(display, root,
		EnterWindowMask | ExposureMask | SubstructureNotifyMask |
		SubstructureRedirectMask | PropertyChangeMask
	);
	XDefineCursor(display, root, XCreateFontCursor(display, XC_left_ptr));

	// Initialize atoms
	atom_init();

	// Initialize root window
	int cardinal = 1;
	XChangeProperty(display, root, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*) &cardinal, 1);
	XChangeProperty(display, root, _NET_DESKTOP_GEOMETRY, XA_CARDINAL, 32, PropModeReplace, (unsigned char*) &screen_width, 1);
	XChangeProperty(display, root, _NET_DESKTOP_GEOMETRY, XA_CARDINAL, 32, PropModeAppend, (unsigned char*) &screen_height, 1);

	// Initialize bar
	bar = XCreateSimpleWindow(display, root, 0, 0, screen_width, bar_height - border_width, 0, 0, 0);
	XChangeProperty(display, bar, _NET_WM_WINDOW_TYPE, XA_ATOM, 32, PropModeReplace, (unsigned char*) &_NET_WM_WINDOW_TYPE_TOOLBAR, 1);
	XSelectInput(display, bar, ExposureMask | ButtonPressMask);
	XConfigureWindow(display, bar, CWBorderWidth, &(XWindowChanges){.border_width=border_width});
	XSetWindowBorder(display, bar, colors[COLOR_BORDER_FOCUS]);
	XMapWindow(display, bar);

	// Colors
	Visual* visual = DefaultVisual(display, DefaultScreen(display));
	Colormap colormap = DefaultColormap(display, DefaultScreen(display));
	xft_draw = XftDrawCreate(display, bar, visual, colormap);
	xft_font = XftFontOpenName(display, DefaultScreen(display), font);

	// Grab necessary input
	int lock_mods[] = { 0, LockMask, 0, LockMask };
	XModifierKeymap* mk = XGetModifierMapping(display);
	for (int i = 0; i < 8; i++) {
		int kpm = mk->max_keypermod;
		for (int j = 0; j < kpm; j++) {
			if (mk->modifiermap[i * kpm + j] == XKeysymToKeycode(display, XK_Num_Lock)) {
				lock_mods[2] |= 1 << i;
				lock_mods[3] |= 1 << i;
			}
		}
	}
	XFreeModifiermap(mk);
	for (int i = 0; i < 4; i++) {
		XGrabButton(display, 1, lock_mods[i], bar, true, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
		XGrabButton(display, 1, lock_mods[i] | Mod4Mask, root, true, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
		XGrabButton(display, 3, lock_mods[i], bar, true, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
		XGrabButton(display, 3, lock_mods[i] | Mod4Mask, root, true, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
	}
	for (size_t i = 0; i < shortcut_count; i++) {
		KeyCode kc = XKeysymToKeycode(display, shortcuts[i].key);
		for (int j = 0; j < 4; j++) {
			XGrabKey(display, kc, lock_mods[j] | shortcuts[i].mask, root, true, GrabModeAsync, GrabModeAsync);
		}
	}

	// Main loop
	XEvent e;
	bool quit = false;
	while (!quit) {
		XNextEvent(display, &e);
		switch (e.type) {
			case ButtonPress: handle_button_press(&e.xbutton); break;
			case ButtonRelease: handle_button_release(&e.xbutton); break;
			case ClientMessage: handle_client_message(&e.xclient); break;
			case ConfigureNotify: break;
			case ConfigureRequest: handle_configure_request(&e.xconfigurerequest); break;
			case CreateNotify: break;
			case DestroyNotify: break;
			case EnterNotify: handle_enter_notify(&e.xcrossing); break;
			case Expose: handle_expose(&e.xexpose); break;
			case KeyPress: quit = handle_key_press(&e.xkey); break;
			case KeyRelease: break;
			case MapNotify: handle_map_notify(&e.xmap); break;
			case MappingNotify: break;
			case MapRequest: handle_map_request(&e.xmaprequest); break;
			case MotionNotify:
				while (XCheckTypedEvent(display, MotionNotify, &e));
				handle_motion_notify(&e.xmotion);
				break;
			case PropertyNotify: handle_property_notify(&e.xproperty); break;
			case UnmapNotify: handle_unmap_notify(&e.xunmap); break;
			default:
				fprintf(stderr, "Unhandled event %d\n", e.type);
				break;
		}
	}

	// Clean up
	XftFontClose(display, xft_font);
	XftDrawDestroy(xft_draw);
	XFreeGC(display, gc);
	XCloseDisplay(display);
	return 0;
}
