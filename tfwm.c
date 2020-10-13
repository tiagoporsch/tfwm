#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
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
	{ XK_a, Mod4Mask,			"dmenu_run" },
	{ XK_e, Mod4Mask,			"st" },
	{ XK_f, Mod4Mask,			"pcmanfm" },
	{ XK_s, Mod4Mask,			"maim | xclip -selection clipboard -t image/png" },
	{ XK_s, Mod4Mask|ShiftMask,	"maim -s | xclip -selection clipboard -t image/png" },
	{ XK_w, Mod4Mask,			"firefox" },
};
static const char* font = "Source Code Pro:style=bold:size=10";
static const char* colors[] = { "#000000", "#FFFFFF" };
static const int screen_width = 1920;
static const int screen_height = 1080;
static const int status_height = 20;

// Global variables
Display* display;
Window root;
XWindowAttributes grab_attr;
XButtonEvent grab_start;
XftDraw* xft_draw;
XftFont* xft_font;
XftColor* xft_colors;

// Status bar
char status_name[128] = "tfwm";
char status[128] = "No status script";

void draw_status() {
	XClearWindow(display, root);
	XftDrawStringUtf8(xft_draw, &xft_colors[1], xft_font,
		2, status_height - 6, (const FcChar8*) status_name, strlen(status_name));
	XftDrawStringUtf8(xft_draw, &xft_colors[1], xft_font,
		screen_width - strlen(status) * xft_font->max_advance_width,
		status_height - 6, (const FcChar8*) status, strlen(status));
}

void update_status_name(Window window) {
	if (window == root) {
		strcpy(status_name, "tfwm");
	} else {
		XTextProperty prop;
		if (XGetTextProperty(display, window, &prop, XA_WM_NAME)) {
			if (prop.encoding == XA_STRING)
				strncpy(status_name, (char*) prop.value, sizeof(status_name) - 1);
			XFree(prop.value);
		}
	}
	draw_status();
}

// Processes
void sigchld_handler(int sig) {
	(void) sig;
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

void spawn(const char* program) {
	if (fork())
		return;
	if (display)
		close(ConnectionNumber(display));
	setsid();
	const char* command[4] = { "/bin/sh", "-c", program, NULL };
	execvp((char*) command[0], (char**) command);
	exit(EXIT_SUCCESS);
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

// Event handlers
bool handle_button_press(XEvent* e) {
	if (e->xbutton.subwindow != None) {
		XRaiseWindow(display, e->xbutton.subwindow);
		XGrabPointer(display, e->xbutton.subwindow, True,
			PointerMotionMask | ButtonReleaseMask, GrabModeAsync,
			GrabModeAsync, None, None, CurrentTime);
		XGetWindowAttributes(display, e->xbutton.subwindow, &grab_attr);
		grab_start = e->xbutton;
	}
	return false;
}

bool handle_button_release(XEvent* e) {
	(void) e;
	XUngrabPointer(display, CurrentTime);
	return false;
}

bool handle_enter_notify(XEvent* e) {
	XSetInputFocus(display, e->xcrossing.window, RevertToPointerRoot, CurrentTime);
	update_status_name(e->xcrossing.window);
	return false;
}

bool handle_expose(XEvent* e) {
	(void) e;
	draw_status();
	return false;
}

bool handle_key_press(XEvent* e) {
	KeySym key = XkbKeycodeToKeysym(display, e->xkey.keycode, 0, 0);
	unsigned int mask = e->xkey.state & (Mod4Mask | ShiftMask);

	if (key == XK_q && mask == (Mod4Mask | ShiftMask)) {
		return true;
	} else if (key == XK_q && mask == Mod4Mask) {
		if (e->xkey.subwindow != None) {
			despawn(e->xkey.subwindow);
		}
	} else if (key == XK_Tab && mask == Mod4Mask) {
		if (e->xkey.subwindow != None) {
			XLowerWindow(display, e->xkey.subwindow);
		}
	} else for (unsigned i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++) {
		struct shortcut shortcut = shortcuts[i];
		if (key == shortcut.key && mask == shortcut.mask) {
			spawn(shortcut.command);
		}
	}
	return false;
}

bool handle_map_request(XEvent* e) {
	Window window = e->xmaprequest.window;
	XSelectInput(display, window, EnterWindowMask);
	XMapWindow(display, window);
	XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
	update_status_name(window);

	XWindowAttributes map_attr;
	XGetWindowAttributes(display, window, &map_attr);
	if (map_attr.width < 100) map_attr.width = screen_width / 2;
	if (map_attr.height < 100) map_attr.height = screen_height / 2;

	XMoveResizeWindow(display, window,
		(screen_width - map_attr.width) / 2,
		(screen_height - map_attr.height) / 2,
		map_attr.width, map_attr.height);
	XRaiseWindow(display, window);
	return false;
}

bool handle_motion_notify(XEvent* e) {
	while (XCheckTypedEvent(display, MotionNotify, e));
	int mx = e->xbutton.x_root;
	int my = e->xbutton.y_root;
	if (grab_start.button == 1) {
		const int max_height = screen_height - status_height;
		if (mx == 0 && my == 0) {
			XMoveResizeWindow(display, e->xmotion.window,
				0, status_height,
				screen_width / 2, max_height / 2);
		} else if (mx == screen_width - 1 && my == 0) {
			XMoveResizeWindow(display, e->xmotion.window,
				screen_width / 2, status_height,
				screen_width / 2, max_height / 2);
		} else if (mx == screen_width - 1 && my == screen_height - 1) {
			XMoveResizeWindow(display, e->xmotion.window,
				screen_width / 2, max_height / 2 + status_height,
				screen_width / 2, max_height / 2);
		} else if (mx == 0 && my == screen_height - 1) {
			XMoveResizeWindow(display, e->xmotion.window,
				0, max_height / 2 + status_height,
				screen_width / 2, max_height / 2);
		} else if (my == 0) {
			XMoveResizeWindow(display, e->xmotion.window,
				0, status_height,
				screen_width, max_height);
		} else if (mx == 0) {
			XMoveResizeWindow(display, e->xmotion.window,
				0, status_height,
				screen_width / 2, max_height);
		} else if (mx == screen_width - 1) {
			XMoveResizeWindow(display, e->xmotion.window,
				screen_width / 2, status_height,
				screen_width / 2, max_height);
		} else {
			XMoveResizeWindow(display, e->xmotion.window,
				grab_attr.x + mx - grab_start.x_root,
				grab_attr.y + my - grab_start.y_root,
				grab_attr.width,
				grab_attr.height);
		}
	} else if (grab_start.button == 3) {
		int width = grab_attr.width + mx - grab_start.x_root;
		int height = grab_attr.height + my - grab_start.y_root;
		if (width < 16) width = 16;
		if (height < 16) height = 16;
		XMoveResizeWindow(display, e->xmotion.window,
			grab_attr.x, grab_attr.y, width, height);
	}
	return false;
}

bool handle_property_notify(XEvent* e) {
	if (e->xproperty.atom == XA_WM_NAME && e->xproperty.window == root) {
		XTextProperty prop;
		if (XGetTextProperty(display, root, &prop, XA_WM_NAME)) {
			if (prop.encoding == XA_STRING) {
				strncpy(status, (char*) prop.value, sizeof(status) - 1);
			}
			XFree(prop.value);
			draw_status();
		}
	}
	return false;
}

// Main
int main() {
	// Manage zombie processes
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = sigchld_handler;
	if (sigaction(SIGCHLD, &action, 0)) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	// Connect to the X server
	display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "Error: XOpenDisplay failed\n");
		exit(EXIT_FAILURE);
	}
	root = DefaultRootWindow(display);
	XSelectInput(display, root, EnterWindowMask | ExposureMask
		| SubstructureNotifyMask | SubstructureRedirectMask
		| PropertyChangeMask);
	XDefineCursor(display, root, XCreateFontCursor(display, XC_left_ptr));

	// Colors
	Visual* visual = DefaultVisual(display, DefaultScreen(display));
	Colormap colormap = DefaultColormap(display, DefaultScreen(display));
	xft_draw = XftDrawCreate(display, root, visual, colormap);
	xft_font = XftFontOpenName(display, DefaultScreen(display), font);
	xft_colors = malloc(sizeof(XftColor) * sizeof(colors) / sizeof(colors[0]));
	for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
		XftColorAllocName(display, visual, colormap, colors[i], &xft_colors[i]);
	XSetWindowBackground(display, root, xft_colors[0].pixel);

	// Get numlock mask
	XModifierKeymap* mk = XGetModifierMapping(display);
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < mk->max_keypermod; j++) {
			if (mk->modifiermap[i * mk->max_keypermod + j] == XKeysymToKeycode(display, XK_Num_Lock)) {
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
	XEvent e;
	bool quit = false;
	while (!quit) {
		XNextEvent(display, &e);
		if (e.type == ButtonPress) quit = handle_button_press(&e);
		else if (e.type == ButtonRelease) quit = handle_button_release(&e);
		else if (e.type == EnterNotify) quit = handle_enter_notify(&e);
		else if (e.type == Expose) quit = handle_expose(&e);
		else if (e.type == KeyPress) quit = handle_key_press(&e);
		else if (e.type == MapRequest) quit = handle_map_request(&e);
		else if (e.type == MotionNotify) quit = handle_motion_notify(&e);
		else if (e.type == PropertyNotify) quit = handle_property_notify(&e);
	}

	// Clean up
	free(xft_colors);
	XCloseDisplay(display);
	return 0;
}
