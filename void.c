#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LENGTH(X)       (sizeof(X) / sizeof((X)[0]))
#define MAX(A, B)       ((A) > (B) ? (A) : (B))
#define MIN(A, B)       ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask) ((mask) & ~(numlockmask | LockMask) & \
                         (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define MOUSEMASK       (ButtonPressMask|ButtonReleaseMask|PointerMotionMask)

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int fx, fy, fw, fh;
    int oldx, oldy, oldw, oldh;
    int bw, oldbw;
    int ws;
    int isfloating;
    int isfullscreen;
    int isurgent;
    Client *next;
};

typedef union {
    int i;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *arg);
    Arg arg;
} Key;

typedef struct {
    const char *name;
    void (*arrange)(void);
} Layout;

static void spawn(const Arg *arg);
static void quit(const Arg *arg);
static void killclient(const Arg *arg);
static void focusstack(const Arg *arg);
static void setmfact(const Arg *arg);
static void incnmaster(const Arg *arg);
static void cyclelayout(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void zoom(const Arg *arg);
static void viewws(const Arg *arg);
static void tagws(const Arg *arg);
static void tile(void);
static void monocle(void);

#include "config.h"

static Display *dpy;
static int screen;
static Window root;
static Window wmcheckwin;
static int sw, sh;
static int running = 1;
static int numlockmask = 0;
static unsigned long colnorm, colsel, colurg;
static Cursor cursor;

static Client *clients = NULL;
static Client *sel = NULL;
static Client *wssel[WORKSPACES];
static int selws = 0;
static int wsnmaster[WORKSPACES];
static float wsmfact[WORKSPACES];
static int wslayout[WORKSPACES];

enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };
enum {
    NetSupported, NetWMState, NetWMStateFullscreen, NetActiveWindow,
    NetWMWindowType, NetWMWindowTypeDialog, NetClientList,
    NetNumberOfDesktops, NetCurrentDesktop, NetWMDesktop,
    NetSupportingWMCheck, NetWMName, NetLast
};

static Atom wmatom[WMLast];
static Atom netatom[NetLast];

static void die(const char *fmt, ...);
static int xerror(Display *d, XErrorEvent *ee);
static int xerrorstart(Display *d, XErrorEvent *ee);
static int xerrordummy(Display *d, XErrorEvent *ee);
static void checkotherwm(void);
static void setup(void);
static void scan(void);
static void run(void);
static void cleanup(void);
static Client *clientfor(Window w);
static void attach(Client *c);
static void detach(Client *c);
static void manage(Window w, XWindowAttributes *wa);
static void unmanage(Client *c, int destroyed);
static void focus(Client *c);
static void unfocus(Client *c, int setfocus);
static Client *firstvisible(void);
static void configure(Client *c);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void arrange(void);
static void updatenumlockmask(void);
static void grabkeys(void);
static void grabbuttons(Client *c);
static Atom getatomprop(Client *c, Atom prop);
static void updatewindowtype(Client *c);
static int sendevent(Client *c, Atom proto);
static void setfullscreen(Client *c, int fullscreen);
static void updateclientlist(void);
static void setnetactivewindow(Window w);
static void updatecurrentdesktop(void);
static void movemouse(Client *c);
static void resizemouse(Client *c);
static void restacklayers(void);

static void keypress(XEvent *e);
static void maprequest(XEvent *e);
static void unmapnotify(XEvent *e);
static void destroynotify(XEvent *e);
static void configurerequest(XEvent *e);
static void configurenotify(XEvent *e);
static void enternotify(XEvent *e);
static void focusin(XEvent *e);
static void propertynotify(XEvent *e);
static void clientmessage(XEvent *e);
static void buttonpress(XEvent *e);

static void (*handler[LASTEvent])(XEvent *e) = {
    [KeyPress] = keypress,
    [MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
    [DestroyNotify] = destroynotify,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [EnterNotify] = enternotify,
    [FocusIn] = focusin,
    [PropertyNotify] = propertynotify,
    [ClientMessage] = clientmessage,
    [ButtonPress] = buttonpress,
};

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static int xerrorstart(Display *d, XErrorEvent *ee) {
    (void)d; (void)ee;
    die("void: another window manager is already running");
    return -1;
}

static int xerrordummy(Display *d, XErrorEvent *ee) {
    (void)d; (void)ee;
    return 0;
}

static int xerror(Display *d, XErrorEvent *ee) {
    if (ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "void: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    (void)d;
    return 0;
}

static void checkotherwm(void) {
    XSetErrorHandler(xerrorstart);
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

static Client *clientfor(Window w) {
    Client *c;
    for (c = clients; c; c = c->next)
        if (c->win == w)
            return c;
    return NULL;
}

static void attach(Client *c) {
    c->next = clients;
    clients = c;
}

static void detach(Client *c) {
    Client **tc;
    for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
    if (*tc)
        *tc = c->next;
}

static Client *firstvisible(void) {
    Client *c;
    for (c = clients; c; c = c->next)
        if (c->ws == selws)
            return c;
    return NULL;
}

static void configure(Client *c) {
    XConfigureEvent ce;
    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = c->bw;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

static void resizeclient(Client *c, int x, int y, int w, int h) {
    c->x = x;
    c->y = y;
    c->w = MAX(w, 1);
    c->h = MAX(h, 1);
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    configure(c);
}

static void tile(void) {
    Client *c;
    int n = 0, i = 0, my = 0, ty = 0, mw, h;

    for (c = clients; c; c = c->next)
        if (c->ws == selws && !c->isfloating && !c->isfullscreen)
            n++;
    if (n == 0)
        return;

    mw = (n > wsnmaster[selws]) ? (int)(sw * wsmfact[selws]) : sw;

    for (c = clients; c; c = c->next) {
        if (c->ws != selws || c->isfloating || c->isfullscreen)
            continue;
        if (i < wsnmaster[selws]) {
            h = (sh - my) / (MIN(n, wsnmaster[selws]) - i) - GAPPX;
            resizeclient(c, GAPPX, my + GAPPX,
                         mw - GAPPX * 2 - 2 * c->bw,
                         h - 2 * c->bw);
            my += h + GAPPX;
        } else {
            h = (sh - ty) / (n - i) - GAPPX;
            resizeclient(c, mw + GAPPX, ty + GAPPX,
                         sw - mw - GAPPX * 2 - 2 * c->bw,
                         h - 2 * c->bw);
            ty += h + GAPPX;
        }
        i++;
    }
}

static void monocle(void) {
    Client *c;
    for (c = clients; c; c = c->next) {
        if (c->ws != selws || c->isfloating || c->isfullscreen)
            continue;
        resizeclient(c, GAPPX, GAPPX,
                     sw - 2 * GAPPX - 2 * c->bw,
                     sh - 2 * GAPPX - 2 * c->bw);
    }
}

static void arrange(void) {
    Client *c;

    for (c = clients; c; c = c->next) {
        if (c->ws == selws)
            XMoveWindow(dpy, c->win, c->x, c->y);
        else
            XMoveWindow(dpy, c->win, -(c->w + 2 * c->bw) * 2 - 100, c->y);
    }
    layouts[wslayout[selws]].arrange();
    for (c = clients; c; c = c->next) {
        if (c->ws == selws && (c->isfloating || c->isfullscreen))
            XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    }
    restacklayers();
}

static void restacklayers(void) {
    Client *c;
    Window *wins;
    int nfloating = 0, ntotal = 0, i = 0;

    for (c = clients; c; c = c->next) {
        if (c->ws == selws) {
            ntotal++;
            if (c->isfloating || c->isfullscreen)
                nfloating++;
        }
    }

    if (ntotal == 0 || nfloating == 0 || nfloating == ntotal)
        return;

    wins = malloc(sizeof(Window) * ntotal);
    if (!wins)
        return;

    for (c = clients; c; c = c->next) {
        if (c->ws == selws) {
            if (c->isfloating || c->isfullscreen)
                wins[i++] = c->win;
        }
    }

    for (c = clients; c; c = c->next) {
        if (c->ws == selws) {
            if (!c->isfloating && !c->isfullscreen)
                wins[i++] = c->win;
        }
    }

    XRestackWindows(dpy, wins, ntotal);
    free(wins);
}

static void unfocus(Client *c, int setfocus) {
    if (!c)
        return;
    XSetWindowBorder(dpy, c->win, c->isurgent ? colurg : colnorm);
    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
}

static int sendevent(Client *c, Atom proto) {
    int n, exists = 0;
    Atom *protocols;
    XEvent ev;

    if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = c->win;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->win, False, NoEventMask, &ev);
    }
    return exists;
}

static void setnetactivewindow(Window w) {
    XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
                     PropModeReplace, (unsigned char *)&w, 1);
}

static void focus(Client *c) {
    if (c && c->ws != selws)
        return;
    if (sel && sel != c)
        unfocus(sel, 0);
    if (c) {
        XSetWindowBorder(dpy, c->win, colsel);
        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        setnetactivewindow(c->win);
        sendevent(c, wmatom[WMTakeFocus]);
        c->isurgent = 0;
        wssel[c->ws] = c;
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
    sel = c;
}

static Atom getatomprop(Client *c, Atom prop) {
    int di;
    unsigned long dl, dl2;
    unsigned char *p = NULL;
    Atom da, atom = None;

    if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof(Atom), False,
                            XA_ATOM, &da, &di, &dl, &dl2, &p) == Success && p) {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

static void setfullscreen(Client *c, int fullscreen) {
    if (fullscreen && !c->isfullscreen) {
        XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                         PropModeReplace,
                         (unsigned char *)&netatom[NetWMStateFullscreen], 1);
        c->isfullscreen = 1;
        c->oldx = c->x; c->oldy = c->y; c->oldw = c->w; c->oldh = c->h;
        c->oldbw = c->bw;
        c->bw = 0;
        resizeclient(c, 0, 0, sw, sh);
        arrange();
    } else if (!fullscreen && c->isfullscreen) {
        XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                         PropModeReplace, (unsigned char *)0, 0);
        c->isfullscreen = 0;
        c->bw = c->oldbw;
        resizeclient(c, c->oldx, c->oldy, c->oldw, c->oldh);
        arrange();
    }
}

static void updatewindowtype(Client *c) {
    Atom state = getatomprop(c, netatom[NetWMState]);
    Atom wtype = getatomprop(c, netatom[NetWMWindowType]);
    Window trans = None;

    if (state == netatom[NetWMStateFullscreen])
        c->isfullscreen = 1;
    if (wtype == netatom[NetWMWindowTypeDialog])
        c->isfloating = 1;
    if (XGetTransientForHint(dpy, c->win, &trans) && trans != None)
        c->isfloating = 1;
}

static void grabbuttons(Client *c) {
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
                ButtonPressMask, GrabModeSync, GrabModeSync, None, None);
    XGrabButton(dpy, Button1, MODKEY, c->win, False,
                ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MODKEY, c->win, False,
                ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
}

static void updateclientlist(void) {
    Client *c;
    XDeleteProperty(dpy, root, netatom[NetClientList]);
    for (c = clients; c; c = c->next)
        XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                         PropModeAppend, (unsigned char *)&c->win, 1);
}

static void manage(Window w, XWindowAttributes *wa) {
    Client *c;
    long d;
    Window trans = None;

    if (clientfor(w))
        return;

    c = calloc(1, sizeof(Client));
    if (!c)
        die("void: calloc failed");

    c->win = w;
    c->x = c->fx = wa->x;
    c->y = c->fy = wa->y;
    c->w = c->fw = wa->width;
    c->h = c->fh = wa->height;
    c->bw = BORDERWIDTH;
    c->ws = selws;

    updatewindowtype(c);

    if (c->isfloating) {
        if (c->w <= 0) c->w = c->fw = sw / 3;
        if (c->h <= 0) c->h = c->fh = sh / 3;
        c->x = c->fx = (sw - c->w) / 2;
        c->y = c->fy = (sh - c->h) / 2;
    }

    XSetWindowBorderWidth(dpy, w, c->bw);
    XSetWindowBorder(dpy, w, colnorm);
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask |
                          PropertyChangeMask | StructureNotifyMask);
    grabbuttons(c);

    attach(c);

    d = c->ws;
    XChangeProperty(dpy, w, netatom[NetWMDesktop], XA_CARDINAL, 32,
                     PropModeReplace, (unsigned char *)&d, 1);

    XMapWindow(dpy, w);

    if (XGetTransientForHint(dpy, w, &trans) && trans != None) {
        Client *parent = clientfor(trans);
        if (parent && parent->ws == selws) {
            c->isfloating = 1;
            if (c->w <= 0) c->w = c->fw = sw / 3;
            if (c->h <= 0) c->h = c->fh = sh / 3;
            c->x = c->fx = (sw - c->w) / 2;
            c->y = c->fy = (sh - c->h) / 2;
        }
    }

    updateclientlist();
    arrange();
    focus(c);
}

static void unmanage(Client *c, int destroyed) {
    detach(c);

    if (!destroyed) {
        XSetErrorHandler(xerrordummy);
        XSelectInput(dpy, c->win, NoEventMask);
        XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
    }

    if (wssel[c->ws] == c)
        wssel[c->ws] = NULL;

    free(c);

    if (sel == c || sel == NULL) {
        sel = NULL;
        focus(firstvisible());
    }

    updateclientlist();
    arrange();
}

static void scan(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa)
            || wa.override_redirect
            || XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa)
            || wa.override_redirect)
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1)
            && wa.map_state == IsViewable)
                manage(wins[i], &wa);
        }
    }
    if (wins)
        XFree(wins);
}

static void updatenumlockmask(void) {
    unsigned int i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    for (i = 0; i < 8; i++)
        for (j = 0; j < (unsigned)modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j]
                == XKeysymToKeycode(dpy, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

static void grabkeys(void) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask | LockMask };
        XUngrabKey(dpy, AnyKey, AnyModifier, root);
        for (i = 0; i < LENGTH(keys); i++) {
            KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
            if (!code)
                continue;
            for (j = 0; j < LENGTH(modifiers); j++)
                XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
                          True, GrabModeAsync, GrabModeAsync);
        }
    }
}

static void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "void: execvp failed for %s\n", ((char **)arg->v)[0]);
        exit(1);
    }
}

static void quit(const Arg *arg) {
    (void)arg;
    running = 0;
}

static void killclient(const Arg *arg) {
    (void)arg;
    if (!sel)
        return;
    if (!sendevent(sel, wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, sel->win);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

static void focusstack(const Arg *arg) {
    Client *c = NULL, *i;

    if (!sel)
        return;

    if (arg->i > 0) {
        for (c = sel->next; c && c->ws != selws; c = c->next);
        if (!c)
            for (c = clients; c && c->ws != selws; c = c->next);
    } else {
        for (i = clients; i && i != sel; i = i->next)
            if (i->ws == selws)
                c = i;
        if (!c)
            for (; i; i = i->next)
                if (i->ws == selws)
                    c = i;
    }
    if (c) {
        focus(c);
    }
}

static void setmfact(const Arg *arg) {
    float f = wsmfact[selws] + arg->f;
    if (f < MFACT_MIN || f > MFACT_MAX)
        return;
    wsmfact[selws] = f;
    arrange();
}

static void incnmaster(const Arg *arg) {
    wsnmaster[selws] = MAX(wsnmaster[selws] + arg->i, 0);
    arrange();
}

static void cyclelayout(const Arg *arg) {
    int n = (int)LENGTH(layouts);
    wslayout[selws] = ((wslayout[selws] + arg->i) % n + n) % n;
    arrange();
}

static void togglefloating(const Arg *arg) {
    (void)arg;
    if (!sel || sel->isfullscreen)
        return;
    sel->isfloating = !sel->isfloating;
    if (sel->isfloating) {
        sel->x = sel->fx;
        sel->y = sel->fy;
        sel->w = sel->fw;
        sel->h = sel->fh;
    } else {
        sel->fx = sel->x;
        sel->fy = sel->y;
        sel->fw = sel->w;
        sel->fh = sel->h;
    }
    arrange();
}

static void togglefullscreen(const Arg *arg) {
    (void)arg;
    if (!sel)
        return;
    setfullscreen(sel, !sel->isfullscreen);
}

static void zoom(const Arg *arg) {
    Client *c = sel, *first;
    (void)arg;

    if (!c || c->isfloating || c->isfullscreen)
        return;

    first = firstvisible();
    if (c == first) {
        for (c = c->next; c && (c->ws != selws || c->isfloating); c = c->next);
        if (!c)
            return;
    }
    detach(c);
    attach(c);
    focus(c);
    arrange();
}

static void updatecurrentdesktop(void) {
    long d = selws;
    XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
                     PropModeReplace, (unsigned char *)&d, 1);
}

static void viewws(const Arg *arg) {
    if (arg->i == selws || arg->i < 0 || arg->i >= WORKSPACES)
        return;
    unfocus(sel, 1);
    selws = arg->i;
    updatecurrentdesktop();
    focus(wssel[selws] ? wssel[selws] : firstvisible());
    arrange();
}

static void tagws(const Arg *arg) {
    Client *c = sel;
    long d;

    if (!c || arg->i == selws || arg->i < 0 || arg->i >= WORKSPACES)
        return;

    c->ws = arg->i;
    d = c->ws;
    XChangeProperty(dpy, c->win, netatom[NetWMDesktop], XA_CARDINAL, 32,
                     PropModeReplace, (unsigned char *)&d, 1);
    if (!wssel[arg->i])
        wssel[arg->i] = c;

    sel = NULL;
    focus(firstvisible());
    arrange();
}

static void movemouse(Client *c) {
    int ocx, ocy, x, y, nx, ny;
    XEvent ev;
    Window dw;
    unsigned int dui;
    int di;

    if (c->isfullscreen)
        return;
    if (!c->isfloating) {
        c->fx = c->x;
        c->fy = c->y;
        c->fw = c->w;
        c->fh = c->h;
        c->isfloating = 1;
        arrange();
    }

    ocx = c->x;
    ocy = c->y;
    if (!XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &dui))
        return;
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync,
                      GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
        return;

    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        if (ev.type == MotionNotify) {
            nx = ocx + (ev.xmotion.x - x);
            ny = ocy + (ev.xmotion.y - y);
            resizeclient(c, nx, ny, c->w, c->h);
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(dpy, CurrentTime);
}

static void resizemouse(Client *c) {
    int ocx, ocy;
    XEvent ev;
    Window dw;
    unsigned int dui;
    int di, x, y;
    int nw, nh;

    if (c->isfullscreen)
        return;
    if (!c->isfloating) {
        c->fx = c->x;
        c->fy = c->y;
        c->fw = c->w;
        c->fh = c->h;
        c->isfloating = 1;
        arrange();
    }

    ocx = c->x;
    ocy = c->y;
    if (!XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &dui))
        return;

    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync,
                      GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
        return;

    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        if (ev.type == MotionNotify) {
            nw = MAX(ev.xmotion.x - ocx, 20);
            nh = MAX(ev.xmotion.y - ocy, 20);
            resizeclient(c, ocx, ocy, nw, nh);
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(dpy, CurrentTime);
}

static void keypress(XEvent *e) {
    XKeyEvent *ev = &e->xkey;
    KeySym keysym = XLookupKeysym(ev, 0);
    unsigned int i;

    for (i = 0; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym
        && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
        && keys[i].func) {
            keys[i].func(&keys[i].arg);
        }
    }
}

static void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;

    if (!XGetWindowAttributes(dpy, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (!clientfor(ev->window))
        manage(ev->window, &wa);
}

static void unmapnotify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    Client *c = clientfor(ev->window);
    if (c && ev->send_event == 0)
        unmanage(c, 0);
}

static void destroynotify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    Client *c = clientfor(ev->window);
    if (c)
        unmanage(c, 1);
}

static void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    Client *c = clientfor(ev->window);
    XWindowChanges wc;

    if (c) {
        if (c->isfloating || c->isfullscreen) {
            if (ev->value_mask & CWX) c->x = ev->x;
            if (ev->value_mask & CWY) c->y = ev->y;
            if (ev->value_mask & CWWidth) c->w = ev->width;
            if (ev->value_mask & CWHeight) c->h = ev->height;
            XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
        }
        configure(c);
    } else {
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

static void configurenotify(XEvent *e) {
    XConfigureEvent *ev = &e->xconfigure;
    if (ev->window == root) {
        sw = ev->width;
        sh = ev->height;
        arrange();
    }
}

static void enternotify(XEvent *e) {
    XCrossingEvent *ev = &e->xcrossing;
    Client *c;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior)
    && ev->window != root)
        return;
    c = clientfor(ev->window);
    if (c && c != sel)
        focus(c);
}

static void focusin(XEvent *e) {
    XFocusChangeEvent *ev = &e->xfocus;
    if (sel && ev->window != sel->win)
        XSetInputFocus(dpy, sel->win, RevertToPointerRoot, CurrentTime);
}

static void propertynotify(XEvent *e) {
    XPropertyEvent *ev = &e->xproperty;
    Client *c = clientfor(ev->window);
    XWMHints *wmh;

    if (!c || ev->atom != XA_WM_HINTS)
        return;

    wmh = XGetWMHints(dpy, c->win);
    if (wmh) {
        if (c == sel) {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(dpy, c->win, wmh);
        } else if (wmh->flags & XUrgencyHint) {
            c->isurgent = 1;
            XSetWindowBorder(dpy, c->win, colurg);
        }
        XFree(wmh);
    }
}

static void clientmessage(XEvent *e) {
    XClientMessageEvent *cme = &e->xclient;
    Client *c = clientfor(cme->window);

    if (!c)
        return;

    if (cme->message_type == netatom[NetWMState]) {
        if ((Atom)cme->data.l[1] == netatom[NetWMStateFullscreen]
        || (Atom)cme->data.l[2] == netatom[NetWMStateFullscreen]) {
            setfullscreen(c, (cme->data.l[0] == 1
                          || (cme->data.l[0] == 2 && !c->isfullscreen)));
        }
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (c != sel && !c->isurgent) {
            if (c->ws != selws) {
                Arg a = {.i = c->ws};
                viewws(&a);
            }
            focus(c);
        }
    }
}

static void buttonpress(XEvent *e) {
    XButtonPressedEvent *ev = &e->xbutton;
    Client *c = clientfor(ev->window);

    if (c && c != sel)
        focus(c);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);

    if (c && CLEANMASK(ev->state) == CLEANMASK(MODKEY)) {
        if (ev->button == Button1)
            movemouse(c);
        else if (ev->button == Button3)
            resizemouse(c);
    }
}

static void setup(void) {
    int i;
    XSetWindowAttributes swa;
    Colormap cmap;
    XColor colex, colhw;

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    for (i = 0; i < WORKSPACES; i++) {
        wsnmaster[i] = NMASTER_DEFAULT;
        wsmfact[i] = MFACT_DEFAULT;
        wslayout[i] = 0;
        wssel[i] = NULL;
    }
    selws = 0;

    cmap = DefaultColormap(dpy, screen);
    XAllocNamedColor(dpy, cmap, COLOR_BORDER_NORM, &colex, &colhw);
    colnorm = colex.pixel;
    XAllocNamedColor(dpy, cmap, COLOR_BORDER_SEL, &colex, &colhw);
    colsel = colex.pixel;
    XAllocNamedColor(dpy, cmap, COLOR_BORDER_URGENT, &colex, &colhw);
    colurg = colex.pixel;

    cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor);

    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);

    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMStateFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    netatom[NetWMDesktop] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    netatom[NetSupportingWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);

    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetSupportingWMCheck], XA_WINDOW,
                     32, PropModeReplace, (unsigned char *)&wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], XInternAtom(dpy, "UTF8_STRING", False),
                     8, PropModeReplace, (unsigned char *)"void", 4);
    XChangeProperty(dpy, root, netatom[NetSupportingWMCheck], XA_WINDOW,
                     32, PropModeReplace, (unsigned char *)&wmcheckwin, 1);

    XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                     PropModeReplace, (unsigned char *)netatom, NetLast);
    XDeleteProperty(dpy, root, netatom[NetClientList]);

    {
        long nd = WORKSPACES, cd = 0;
        XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL,
                         32, PropModeReplace, (unsigned char *)&nd, 1);
        XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL,
                         32, PropModeReplace, (unsigned char *)&cd, 1);
    }

    grabkeys();

    swa.cursor = cursor;
    swa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                      EnterWindowMask | PropertyChangeMask | StructureNotifyMask;
    XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &swa);
    XSelectInput(dpy, root, swa.event_mask);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    XSync(dpy, False);
}

static void run(void) {
    XEvent ev;
    XSync(dpy, False);
    while (running && !XNextEvent(dpy, &ev)) {
        if (handler[ev.type])
            handler[ev.type](&ev);
    }
}

static void cleanup(void) {
    Arg a = {.i = 0};
    Client *c;
    int i;

    (void)a;
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XFreeCursor(dpy, cursor);
    while (clients) {
        c = clients;
        clients = clients->next;
        free(c);
    }
    for (i = 0; i < WORKSPACES; i++)
        wssel[i] = NULL;
    XDestroyWindow(dpy, wmcheckwin);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(dpy, False);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp(argv[1], "-v"))
        die("void-wm-1.0");
    else if (argc != 1)
        die("usage: void [-v]");

    if (!(dpy = XOpenDisplay(NULL)))
        die("void: cannot open display");

    checkotherwm();
    setup();
    scan();
    run();
    cleanup();

    XCloseDisplay(dpy);
    return 0;
}
