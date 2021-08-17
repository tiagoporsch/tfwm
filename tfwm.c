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
	{ XK_q,     Mod4Mask, "!close" },
	{ XK_q,     Mod4Mask|ShiftMask, "!quit" },

	{ XK_Tab, Mod1Mask, "rofi -show window" },
	{ XK_a,	  Mod4Mask, "rofi -show run" },
	{ XK_e,   Mod4Mask, "xfce4-terminal" },
	{ XK_s,   Mod4Mask, "maim | xclip -selection clipboard -t image/png" },
	{ XK_s,   Mod4Mask|ShiftMask, "maim -s | xclip -selection clipboard -t image/png" },
};
static const size_t shortcut_count = sizeof(shortcuts) / sizeof(shortcuts[0]);

static const char* font = "Source Code Pro:style=bold:size=10";
static const char* colors[] = { "#333333", "#FFFFFF" };
static const int bar_height = 20;

// Global variables
int screen_width;
int screen_height;

Display* display;
GC gc;
Window root;
Window focused;

XftDraw* xft_draw;
XftFont* xft_font;
XftColor* xft_colors;

// Logging
FILE* log_file = NULL;

void log_init() {
	log_file = fopen("tfwm.log", "w");
}

void log_cleanup() {
	if (log_file)
		fclose(log_file);
}

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

// Client
typedef struct client {
	Window window;
	bool anchored;
	int w, h;
	struct client* next;
} client_t;
client_t* clients = NULL;

client_t* client_find(Window window) {
	for (client_t* c = clients; c; c = c->next)
		if (c->window == window)
			return c;
	return NULL;
}

// Status
char status_left[128] = { 0 };
char status_right[128] = { 0 };

void status_draw() {
	XSetForeground(display, gc, xft_colors[0].pixel);
	XFillRectangle(display, root, gc, 0, 0, screen_width, bar_height);
	XftDrawStringUtf8(xft_draw, &xft_colors[1], xft_font,
		4, bar_height - 6, (XftChar8*) status_left, strlen(status_left));
	XftDrawStringUtf8(xft_draw, &xft_colors[1], xft_font,
		screen_width - strlen(status_right) * xft_font->max_advance_width,
		bar_height - 6, (XftChar8*) status_right, strlen(status_right));
}

// Window
void window_get_title(Window window, char* buffer, size_t size) {
	XTextProperty prop;
	if (!XGetTextProperty(display, window, &prop, XA_WM_NAME))
		return;
	if (prop.encoding == XA_STRING) {
		strncpy(buffer, (char*) prop.value, size);
	} else {
		char** list = NULL;
		int count = 0;
		int ret = XmbTextPropertyToTextList(display, &prop, &list, &count);
		if (ret >= Success && count > 0 && list[0] != NULL) {
			strncpy(buffer, list[0], size);
			XFreeStringList(list);
		}
	}
	XFree(prop.value);
}

void window_focus(Window window) {
	if (focused == window)
		return;
	focused = window;
	XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
	if (window == root)
		status_left[0] = 0;
	else
		window_get_title(window, status_left, sizeof(status_left));
	status_draw();
}

void window_close(Window window) {
	const Atom WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", false);
	const Atom WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", false);

	Atom* protocols;
	int protocol_count;
	if (XGetWMProtocols(display, window, &protocols, &protocol_count)) {
		for (int i = 0; i < protocol_count; i++) {
			if (protocols[i] != WM_DELETE_WINDOW)
				continue;
			XEvent event;
			event.type = ClientMessage;
			event.xclient.window = window;
			event.xclient.message_type = WM_PROTOCOLS;
			event.xclient.format = 32;
			event.xclient.data.l[0] = WM_DELETE_WINDOW;
			event.xclient.data.l[1] = CurrentTime;
			XSendEvent(display, window, false, NoEventMask, &event);
			XFree(protocols);
			return;
		}
		XFree(protocols);
	}

	XGrabServer(display);
	XSetCloseDownMode(display, DestroyAll);
	XKillClient(display, window);
	XSync(display, false);
	XUngrabServer(display);
}

// Event handlers
XWindowAttributes grab_attr;
XButtonEvent grab_start;

void handle_button_press(XButtonEvent* e) {
	client_t* c = client_find(e->subwindow);
	if (c == NULL)
		return;
	XRaiseWindow(display, e->subwindow);
	XGrabPointer(display, e->subwindow, true, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	XGetWindowAttributes(display, e->subwindow, &grab_attr);
	if (grab_attr.width != c->w || grab_attr.height != c->h) {
		grab_attr.x = e->x_root - c->w / 2;
		grab_attr.y = e->y_root - c->h / 2;
	}
	grab_attr.width = c->w;
	grab_attr.height = c->h;
	grab_start = *e;
}

void handle_button_release(XButtonEvent* e) {
	(void) e;
	XUngrabPointer(display, CurrentTime);
}

void handle_client_message(XClientMessageEvent* e) {
	const Atom _NET_ACTIVE_WINDOW = XInternAtom(display, "_NET_ACTIVE_WINDOW", false);
	if (e->message_type == _NET_ACTIVE_WINDOW) {
		XRaiseWindow(display, e->window);
		window_focus(e->window);
	}
}

void handle_configure_request(XConfigureRequestEvent* e) {
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
	window_focus(e->window);
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
		if (!strcmp(shortcut.command, "!close")) {
			if (e->subwindow != None) window_close(e->subwindow);
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
	window_focus(e->window);

	client_t* c = malloc(sizeof(*c));
	c->window = e->window;
	c->anchored = false;
	c->next = clients;
	clients = c;

	XWindowAttributes attr;
	XGetWindowAttributes(display, e->window, &attr);
	if (attr.width > screen_width) c->w = screen_width;
	else if (attr.width < 16) c->w = 16;
	else c->w = attr.width;
	if (attr.height > screen_height - bar_height) c->h = screen_height - bar_height;
	else if (attr.height < 16) c->h = 16;
	else c->h = attr.height;
	if (attr.x == 0) attr.x = (screen_width - c->w) / 2;
	if (attr.y == 0) attr.y = (screen_height - bar_height - c->h) / 2 + bar_height;
	else if (attr.y < bar_height) attr.y = bar_height;
	XMoveResizeWindow(display, e->window, attr.x, attr.y, c->w, c->h);
}

void handle_motion_notify(XMotionEvent* e) {
	client_t* c = client_find(e->window);
	if (c == NULL)
		return;
	int mx = e->x_root;
	int my = e->y_root;
	if (grab_start.button == 1) {
		const int max_height = screen_height - bar_height;
		if (mx == 0 && my == 0) {
			XMoveResizeWindow(display, e->window,
				0, bar_height,
				screen_width / 2, max_height / 2);
			c->anchored = true;
		} else if (mx == screen_width - 1 && my == 0) {
			XMoveResizeWindow(display, e->window,
				screen_width / 2, bar_height,
				screen_width / 2, max_height / 2);
			c->anchored = true;
		} else if (mx == screen_width - 1 && my == screen_height - 1) {
			XMoveResizeWindow(display, e->window,
				screen_width / 2, max_height / 2 + bar_height,
				screen_width / 2, max_height / 2);
			c->anchored = true;
		} else if (mx == 0 && my == screen_height - 1) {
			XMoveResizeWindow(display, e->window,
				0, max_height / 2 + bar_height,
				screen_width / 2, max_height / 2);
			c->anchored = true;
		} else if (my == 0) {
			XMoveResizeWindow(display, e->window,
				0, bar_height,
				screen_width, max_height);
			c->anchored = true;
		} else if (mx == 0) {
			XMoveResizeWindow(display, e->window,
				0, bar_height,
				screen_width / 2, max_height);
			c->anchored = true;
		} else if (mx == screen_width - 1) {
			XMoveResizeWindow(display, e->window,
				screen_width / 2, bar_height,
				screen_width / 2, max_height);
			c->anchored = true;
		} else {
			XMoveResizeWindow(display, e->window,
				grab_attr.x + mx - grab_start.x_root,
				grab_attr.y + my - grab_start.y_root,
				grab_attr.width,
				grab_attr.height);
			c->anchored = false;
		}
	} else if (grab_start.button == 3 && !c->anchored) {
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
	if (e->window == root)
		window_get_title(e->window, status_right, sizeof(status_right));
	else
		window_get_title(e->window, status_left, sizeof(status_left));
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
}

// Main
int main() {
	log_init();

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
	focused = root = DefaultRootWindow(display);
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
			ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None
		);
		XGrabButton(
			display, 3, lock_mods[i] | Mod4Mask, root, true,
			ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None
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
