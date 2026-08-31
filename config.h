#ifndef CONFIG_H
#define CONFIG_H

#define WORKSPACES 10

#define MODKEY Mod4Mask

#define BORDERWIDTH 4
#define GAPPX 2

#define COLOR_BORDER_NORM "#111D4A"
#define COLOR_BORDER_SEL "#111D4A"
#define COLOR_BORDER_URGENT "#ff0000"

#define NMASTER_DEFAULT 1
#define MFACT_DEFAULT 0.6f
#define MFACT_MIN 0.1f
#define MFACT_MAX 0.9f
#define MFACT_STEP 0.05f

static const char *termcmd[] = { "st", NULL };
static const char *menucmd[] = { "rofi", "-show", "drun", NULL };

static Layout layouts[] = {
    { "tile", tile },
    { "monocle", monocle },
};

#define TAGKEYS(KEY,TAG) \
    { MODKEY,                       XK_##KEY, viewws,      {.i = TAG} }, \
    { MODKEY|ShiftMask,             XK_##KEY, tagws,       {.i = TAG} },

static Key keys[] = {
    { MODKEY,                       XK_Return, spawn,      {.v = termcmd} },
    { MODKEY,                       XK_d,      spawn,      {.v = menucmd} },
    { MODKEY,                       XK_q,      killclient, {0} },
    { MODKEY|ShiftMask,             XK_Delete, quit,       {0} },
    { MODKEY,                       XK_j,      focusstack, {.i = +1} },
    { MODKEY,                       XK_k,      focusstack, {.i = -1} },
    { MODKEY,                       XK_h,      setmfact,   {.f = -MFACT_STEP} },
    { MODKEY,                       XK_l,      setmfact,   {.f = +MFACT_STEP} },
    { MODKEY,                       XK_i,      incnmaster, {.i = +1} },
    { MODKEY,                       XK_u,      incnmaster, {.i = -1} },
    { MODKEY,                       XK_space,  cyclelayout,{.i = +1} },
    { MODKEY,                       XK_t,      togglefloating, {0} },
    { MODKEY,                       XK_f,      togglefullscreen, {0} },
    { MODKEY|ShiftMask,             XK_Return, zoom,       {0} },
    TAGKEYS(1, 0)
    TAGKEYS(2, 1)
    TAGKEYS(3, 2)
    TAGKEYS(4, 3)
    TAGKEYS(5, 4)
    TAGKEYS(6, 5)
    TAGKEYS(7, 6)
    TAGKEYS(8, 7)
    TAGKEYS(9, 8)
    TAGKEYS(0, 9)
};

#endif
