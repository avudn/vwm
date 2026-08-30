#ifndef CONFIG_H
#define CONFIG_H

#define WORKSPACES      10
#define MODKEY          Mod4Mask
#define BORDERWIDTH     1
#define GAPPX           0
#define NMASTER_DEFAULT 1
#define MFACT_DEFAULT   0.55f
#define MFACT_MIN       0.05f
#define MFACT_MAX       0.95f
#define MFACT_STEP      0.05f
#define COLOR_BORDER_NORM   "#444444"
#define COLOR_BORDER_SEL    "#88aaff"
#define COLOR_BORDER_URGENT "#ff3333"
#define TERMCMD  { "st", NULL }
#define MENUCMD { "rofi", "-show", "drun", NULL }

static const char *termcmd[]  = TERMCMD;
static const char *menucmd[] = MENUCMD;

static const Layout layouts[] = {
    { "tile",    tile },
    { "monocle", monocle },
};

#define TAGKEYS(KEY,IDX) \
    { MODKEY,                KEY, viewws,   {.i = (IDX)} }, \
    { MODKEY|ShiftMask,      KEY, tagws,    {.i = (IDX)} },

static const Key keys[] = {
    { MODKEY,               XK_Return, spawn,            {.v = termcmd} },
    { MODKEY,               XK_d,      spawn,            {.v = menucmd} },
    { MODKEY|ShiftMask,     XK_Delete,      quit,             {0} },
    { MODKEY,     	    XK_q,      killclient,       {0} },
    { MODKEY,               XK_j,      focusstack,       {.i = +1} },
    { MODKEY,               XK_k,      focusstack,       {.i = -1} },
    { MODKEY,               XK_h,      setmfact,         {.f = -MFACT_STEP} },
    { MODKEY,               XK_l,      setmfact,         {.f = +MFACT_STEP} },
    { MODKEY,               XK_i,      incnmaster,       {.i = +1} },
    { MODKEY,               XK_u,      incnmaster,       {.i = -1} },
    { MODKEY,               XK_space,  cyclelayout,      {.i = +1} },
    { MODKEY,               XK_t,      togglefloating,   {0} },
    { MODKEY,               XK_f,      togglefullscreen, {0} },
    { MODKEY|ShiftMask,     XK_Return, zoom,             {0} },
    TAGKEYS(XK_1, 0)
    TAGKEYS(XK_2, 1)
    TAGKEYS(XK_3, 2)
    TAGKEYS(XK_4, 3)
    TAGKEYS(XK_5, 4)
    TAGKEYS(XK_6, 5)
    TAGKEYS(XK_7, 6)
    TAGKEYS(XK_8, 7)
    TAGKEYS(XK_9, 8)
    TAGKEYS(XK_0, 9)
};

#endif
