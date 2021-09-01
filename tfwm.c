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

// Config
static const struct shortcut {
	KeySym key;
	unsigned int mask;
	const char* command;
} shortcuts[] = {
	{ XK_Tab,   Mod4Mask, "!lower" },
	{ XK_Tab,   Mod1Mask, "!change" },
	{ XK_q,     Mod4Mask, "!close" },
	{ XK_w,     Mod4Mask, "!hide" },
	{ XK_q,     Mod4Mask|ShiftMask, "!quit" },

	{ XK_a,	  Mod4Mask, "rofi -show run" },
	{ XK_e,   Mod4Mask, "xfce4-terminal" },
	{ XK_s,   Mod4Mask, "maim | xclip -selection clipboard -t image/png" },
	{ XK_s,   Mod4Mask|ShiftMask, "maim -s | xclip -selection clipboard -t image/png" },
};
static const size_t shortcut_count = sizeof(shortcuts) / sizeof(shortcuts[0]);

static const char* font = "Source Code Pro:style=bold:size=10";
static const char* colors[] = { "#333333", "#999999", "#FFFFFF" };
static const int bar_height = 22;

// Definitions
#define CLIENT_NAME_LENGTH 15

enum anchor {
	ANCHOR_NONE, ANCHOR_TOP,
	ANCHOR_TOP_LEFT, ANCHOR_TOP_RIGHT,
	ANCHOR_LEFT, ANCHOR_RIGHT,
	ANCHOR_BOT_LEFT, ANCHOR_BOT_RIGHT,
};

typedef struct client {
	Window window;
	char name[CLIENT_NAME_LENGTH + 1];
	bool hidden;
	int x, y, w, h;
	int px, py, pw, ph;
	enum anchor anchor;
	struct client* next;
} client_t;

// Global variables
int screen_width;
int screen_height;
int view_height;

Display* display;
GC gc;
Window root;

XftDraw* xft_draw;
XftFont* xft_font;
XftColor* xft_colors;

client_t* clients = NULL;
client_t* focused;

// Atoms
Atom _NET_ACTIVE_WINDOW;
Atom _NET_WM_STATE;
Atom _NET_WM_STATE_HIDDEN;
Atom _NET_WM_STATE_MAXIMIZED_VERT;
Atom _NET_WM_STATE_MAXIMIZED_HORZ;
Atom WM_CHANGE_STATE;
Atom WM_DELETE_WINDOW;
Atom WM_PROTOCOLS;
Atom WM_STATE;

void atom_init() {
	_NET_ACTIVE_WINDOW = XInternAtom(display, "_NET_ACTIVE_WINDOW", false);
	_NET_WM_STATE = XInternAtom(display, "_NET_WM_STATE", false);
	_NET_WM_STATE_HIDDEN = XInternAtom(display, "_NET_WM_STATE_HIDDEN", false);
	_NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", false);
	_NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", false);
	WM_CHANGE_STATE = XInternAtom(display, "WM_CHANGE_STATE", false);
	WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", false);
	WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", false);
	WM_STATE = XInternAtom(display, "WM_STATE", false);
}

// Logging
FILE* log_file = NULL;

void log_info(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(log_file ? log_file : stderr, format, args);
	va_end(args);
	if (log_file)
		fflush(log_file);
}

int log_error_event(Display* d, XErrorEvent* e) {
	char msg[256];
	XGetErrorText(d, e->error_code, msg, sizeof(msg));
	log_info("error: %s (request %d)\n", msg, e->request_code);
	return 0;
}

void log_init(const char* filename) {
	log_file = fopen(filename, "w");
	if (!log_file)
		log_info("error: couldn't open log file\n");
}

void log_cleanup() {
	if (log_file)
		fclose(log_file);
}

// Status
char status_str[128] = { 0 };

void status_draw() {
	XSetForeground(display, gc, xft_colors[0].pixel);
	XFillRectangle(display, root, gc, 0, 0, screen_width, bar_height);
	int x = 4;
	for (client_t* c = clients; c; c = c->next) {
		XftColor* color = &xft_colors[c == focused ? 2 : 1];
		XftDrawStringUtf8(xft_draw, color, xft_font,
			x, bar_height - 6,
			(XftChar8*) c->name, strlen(c->name)
		);
		int advance = CLIENT_NAME_LENGTH * xft_font->max_advance_width;
		XSetForeground(display, gc, color->pixel);
		XDrawLine(display, root, gc, x, bar_height - 2, x + advance, bar_height - 2);
		x += advance + 8;
	}
	XftDrawStringUtf8(xft_draw, &xft_colors[2], xft_font,
		screen_width - strlen(status_str) * xft_font->max_advance_width,
		bar_height - 6, (XftChar8*) status_str, strlen(status_str));
}

// Client
client_t* client_find(Window window) {
	for (client_t* c = clients; c; c = c->next)
		if (c->window == window)
			return c;
	return NULL;
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
	if (x == -1) x = c->x; else c->x = x;
	if (y == -1) y = c->y; else c->y = y;
	XMoveWindow(display, c->window, x, y);
}

void client_move_resize(client_t* c, int x, int y, int w, int h) {
	if (x == -1) x = c->x; else c->x = x;
	if (y == -1) y = c->y; else c->y = y;
	if (w == -1) w = c->w; else c->w = w;
	if (h == -1) h = c->h; else c->h = h;
	XMoveResizeWindow(display, c->window, x, y, w, h);
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

void client_anchor(client_t* c, enum anchor anchor) {
	if (c->anchor == anchor) return;
	if (c->anchor == ANCHOR_NONE) {
		c->px = c->x;
		c->py = c->y;
		c->pw = c->w;
		c->ph = c->h;
	}
	switch (anchor) {
		case ANCHOR_NONE: if (c->anchor != ANCHOR_NONE) client_move_resize(c, c->px, c->py, c->pw, c->ph); break;
		case ANCHOR_TOP: client_move_resize(c, 0, bar_height, screen_width, view_height); break;
		case ANCHOR_TOP_LEFT: client_move_resize(c, 0, bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_TOP_RIGHT: client_move_resize(c, screen_width / 2, bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_LEFT: client_move_resize(c, 0, bar_height, screen_width / 2, view_height); break;
		case ANCHOR_RIGHT: client_move_resize(c, screen_width / 2, bar_height, screen_width / 2, view_height); break;
		case ANCHOR_BOT_LEFT: client_move_resize(c, screen_width / 2, view_height / 2 + bar_height, screen_width / 2, view_height / 2); break;
		case ANCHOR_BOT_RIGHT: client_move_resize(c, screen_width / 2, view_height / 2 + bar_height, screen_width / 2, view_height / 2); break;
	}
	c->anchor = anchor;
	client_update_state(c);
}

void client_focus(client_t* c) {
	focused = c;
	XSetInputFocus(display, c ? c->window : root, RevertToPointerRoot, CurrentTime);
	status_draw();
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
	// Clicking on top bar
	if (e->y_root < bar_height) {
		int x = 4;
		int advance = CLIENT_NAME_LENGTH * xft_font->max_advance_width;
		for (client_t* c = clients; c; c = c->next, x += advance + 8) {
			if (e->x_root - x >= advance)
				continue;
			if (e->button == 1)
				client_raise(c);
			else if (e->button == 3)
				client_close(c);
			break;
		}
		return;
	}

	// Moving and resizing
	client_t* c = client_find(e->subwindow);
	if (c == NULL)
		return;
	XRaiseWindow(display, c->window);
	XGrabPointer(display, c->window, false, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	XGetWindowAttributes(display, c->window, &grab_attr);
	grab_start = *e;
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

	if (e->message_type == _NET_ACTIVE_WINDOW) {
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
			log_info("Unhandled _NET_WM_STATE(%d, %s, %s)\n", e->data.l[0], n1, n2);
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
		log_info("Unhandled client message %s\n", n);
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
	if (e->y_root >= bar_height)
		client_focus(client_find(e->window));
	log_info("Enter Notify\n");
}

void handle_expose(XExposeEvent* e) {
	if (e->window == root) {
		XClearArea(display, root, e->x, e->y, e->width, e->height, false);
		if (e->y < bar_height)
			status_draw();
	}
}

bool handle_key_press(XKeyEvent* e) {
	KeySym key = XkbKeycodeToKeysym(display, e->keycode, 0, 0);
	unsigned int mask = e->state & (Mod1Mask | Mod4Mask | ShiftMask);
	for (size_t i = 0; i < shortcut_count; i++) {
		struct shortcut shortcut = shortcuts[i];
		if (key != shortcut.key || mask != shortcut.mask)
			continue;
		if (!strcmp(shortcut.command, "!change")) {
			client_t* c = focused ? focused : clients;
			if (c) client_raise(c->next ? c->next : clients);
		} else if (!strcmp(shortcut.command, "!close")) {
			client_t* c = client_find(e->subwindow);
			if (c) client_close(c);
		} else if (!strcmp(shortcut.command, "!hide")) {
			client_t* c = client_find(e->subwindow);
			if (c) client_hide(c);
		} else if (!strcmp(shortcut.command, "!lower")) {
			if (e->subwindow != None) XLowerWindow(display, e->subwindow);
		} else if (!strcmp(shortcut.command, "!quit")) {
			return true;
		} else {
			if (fork())
				return false;
			if (display)
				close(ConnectionNumber(display));
			setsid();
			const char* command[4] = { "/bin/sh", "-c", shortcut.command, NULL };
			execvp((char*) command[0], (char**) command);
			exit(EXIT_SUCCESS);
		}
	}
	return false;
}

void handle_map_request(XMapRequestEvent* e) {
	XAddToSaveSet(display, e->window);
	XRaiseWindow(display, e->window);
	XSelectInput(display, e->window, EnterWindowMask | PropertyChangeMask);
	XMapWindow(display, e->window);

	const Atom _NET_CLIENT_LIST = XInternAtom(display, "_NET_CLIENT_LIST", false);
	XChangeProperty(display, root, _NET_CLIENT_LIST, XA_WINDOW, 32,
		PropModeAppend, (unsigned char*) &e->window, 1
	);

	client_t* c = malloc(sizeof(*c));
	c->window = e->window;
	window_get_title(c->window, c->name, sizeof(c->name));
	c->anchor = ANCHOR_NONE;
	c->hidden = true;
	c->next = clients;
	clients = c;
	client_update_state(c);

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

	client_move_resize(c, x, y, w, h);
	client_show(c);
}

void handle_motion_notify(XMotionEvent* e) {
	client_t* c = client_find(e->window);
	if (c == NULL)
		return;
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
		c->w = grab_attr.width + mx - grab_start.x_root;
		c->h = grab_attr.height + my - grab_start.y_root;
		if (c->w < 16) c->w = 16;
		if (c->h < 16) c->h = 16;
		XResizeWindow(display, e->window, c->w, c->h);
	}
}

void handle_property_notify(XPropertyEvent* e) {
	if (e->atom != XA_WM_NAME)
		return;
	if (e->window == root) {
		window_get_title(e->window, status_str, sizeof(status_str));
	} else {
		client_t* c = client_find(e->window);
		if (!c) return;
		window_get_title(c->window, c->name, sizeof(c->name));
	}
	status_draw();
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

	const Atom _NET_CLIENT_LIST = XInternAtom(display, "_NET_CLIENT_LIST", false);
	XDeleteProperty(display, root, _NET_CLIENT_LIST);
	for (client_t* c = clients; c; c = c->next) {
		XChangeProperty(display, root, _NET_CLIENT_LIST, XA_WINDOW, 32,
			PropModeAppend, (unsigned char*) &c->window, 1
		);
	}
	status_draw();
}

// Main
int main(int argc, char** argv) {
	// Parse arguments
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--log")) {
			if (i == argc - 1) {
				log_info("error: invalid arguments\n");
				exit(EXIT_FAILURE);
			}
			log_init(argv[++i]);
		} else {
			log_info("error: invalid arguments\n");
			exit(EXIT_FAILURE);
		}
	}

	// Manage zombie processes
	signal(SIGCHLD, SIG_IGN);

	// Connect to the X server
	display = XOpenDisplay(NULL);
	if (display == NULL) {
		log_info("error: XOpenDisplay failed\n");
		exit(EXIT_FAILURE);
	}
	XSetErrorHandler(log_error_event);
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

	// Colors
	Visual* visual = DefaultVisual(display, DefaultScreen(display));
	Colormap colormap = DefaultColormap(display, DefaultScreen(display));
	xft_draw = XftDrawCreate(display, root, visual, colormap);
	xft_font = XftFontOpenName(display, DefaultScreen(display), font);
	xft_colors = malloc(sizeof(XftColor) * sizeof(colors) / sizeof(colors[0]));
	for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
		XftColorAllocName(display, visual, colormap, colors[i], &xft_colors[i]);

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
		XGrabButton(
			display, 1, lock_mods[i] | Mod4Mask, root, true,
			ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None
		);
		XGrabButton(
			display, 3, lock_mods[i] | Mod4Mask, root, true,
			ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None
		);
	}
	for (size_t i = 0; i < shortcut_count; i++) {
		KeyCode kc = XKeysymToKeycode(display, shortcuts[i].key);
		for (int j = 0; j < 4; j++) {
			XGrabKey(
				display, kc, lock_mods[j] | shortcuts[i].mask,
				root, true, GrabModeAsync, GrabModeAsync
			);
		}
	}

	// Initialize atoms
	atom_init();

	// Main loop
	XEvent e;
	bool quit = false;
	while (!quit) {
		XNextEvent(display, &e);
		switch (e.type) {
			case ButtonPress: handle_button_press(&e.xbutton); break;
			case ButtonRelease: handle_button_release(&e.xbutton); break;
			case ClientMessage: handle_client_message(&e.xclient); break;
			case ConfigureRequest: handle_configure_request(&e.xconfigurerequest); break;
			case EnterNotify: handle_enter_notify(&e.xcrossing); break;
			case Expose: handle_expose(&e.xexpose); break;
			case KeyPress: quit = handle_key_press(&e.xkey); break;
			case MapRequest: handle_map_request(&e.xmaprequest); break;
			case MotionNotify:
				while (XCheckTypedEvent(display, MotionNotify, &e));
				handle_motion_notify(&e.xmotion);
				break;
			case PropertyNotify: handle_property_notify(&e.xproperty); break;
			case UnmapNotify: handle_unmap_notify(&e.xunmap); break;
			default: break;
		}
	}

	// Clean up
	free(xft_colors);
	XftFontClose(display, xft_font);
	XftDrawDestroy(xft_draw);
	XFreeGC(display, gc);
	XCloseDisplay(display);
	log_cleanup();
	return 0;
}
