/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include <stdio.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <common/arith.h>
#include <common/color.h>
#include <common/colorprocs.h>
#include <common/colortbl.h>
#include <common/memory.h>
#include <cgraph/strcasecmp.h>
#include <cgraph/unreachable.h>

static char* colorscheme;

static void hsv2rgb(double h, double s, double v,
			double *r, double *g, double *b)
{
    int i;
    double f, p, q, t;

    if (s <= 0.0) {		/* achromatic */
	*r = v;
	*g = v;
	*b = v;
    } else {
	if (h >= 1.0)
	    h = 0.0;
	h = 6.0 * h;
	i = (int) h;
	f = h - (double) i;
	p = v * (1 - s);
	q = v * (1 - s * f);
	t = v * (1 - s * (1 - f));
	switch (i) {
	case 0:
	    *r = v;
	    *g = t;
	    *b = p;
	    break;
	case 1:
	    *r = q;
	    *g = v;
	    *b = p;
	    break;
	case 2:
	    *r = p;
	    *g = v;
	    *b = t;
	    break;
	case 3:
	    *r = p;
	    *g = q;
	    *b = v;
	    break;
	case 4:
	    *r = t;
	    *g = p;
	    *b = v;
	    break;
	case 5:
	    *r = v;
	    *g = p;
	    *b = q;
	    break;
	}
    }
}

static void rgb2hsv(double r, double g, double b,
		double *h, double *s, double *v)
{

    double rgbmin, rgbmax;
    double rc, bc, gc;
    double ht = 0.0, st = 0.0;

    rgbmin = fmin(r, fmin(g, b));
    rgbmax = fmax(r, fmax(g, b));

    if (rgbmax > 0.0)
	st = (rgbmax - rgbmin) / rgbmax;

    if (st > 0.0) {
	rc = (rgbmax - r) / (rgbmax - rgbmin);
	gc = (rgbmax - g) / (rgbmax - rgbmin);
	bc = (rgbmax - b) / (rgbmax - rgbmin);
	if (r == rgbmax)
	    ht = bc - gc;
	else if (g == rgbmax)
	    ht = 2 + rc - bc;
	else if (b == rgbmax)
	    ht = 4 + gc - rc;
	ht = ht * 60.0;
	if (ht < 0.0)
	    ht += 360.0;
    }
    *h = ht / 360.0;
    *v = rgbmax;
    *s = st;
}

static void rgb2cmyk(double r, double g, double b, double *c, double *m,
		     double *y, double *k)
{
    *c = 1.0 - r;
    *m = 1.0 - g;
    *y = 1.0 - b;
    *k = fmin(*c, *m);
    *k = fmin(*y, *k);
    *c -= *k;
    *m -= *k;
    *y -= *k;
}

static int colorcmpf(const void *p0, const void *p1)
{
    return strcasecmp(((const hsvrgbacolor_t *) p0)->name, ((const hsvrgbacolor_t *) p1)->name);
}

char *canontoken(char *str)
{
    static char *canon;
    static size_t allocated;
    char c, *p, *q;
    size_t len;

    p = str;
    len = strlen(str);
    if (len >= allocated) {
	allocated = len + 1 + 10;
	canon = grealloc(canon, allocated);
    }
    q = canon;
    while ((c = *p++)) {
	if (isupper(c))
	    c = (char)tolower(c);
	*q++ = c;
    }
    *q = '\0';
    return canon;
}

/* fullColor:
 * Return "/prefix/str"
 */
static char* fullColor (char* prefix, char* str)
{
    static char *fulls;
    static size_t allocated;
    size_t len = strlen(prefix) + strlen(str) + 3;

    if (len >= allocated) {
	allocated = len + 10;
	fulls = grealloc(fulls, allocated);
    }
    sprintf (fulls, "/%s/%s", prefix, str);
    return fulls;
}

/* resolveColor:
 * Resolve input color str allowing color scheme namespaces.
 *  0) "black" => "black" 
 *     "white" => "white" 
 *     "lightgrey" => "lightgrey" 
 *    NB: This is something of a hack due to the remaining codegen.
 *        Once these are gone, this case could be removed and all references
 *        to "black" could be replaced by "/X11/black".
 *  1) No initial / => 
 *          if colorscheme is defined and no "X11", return /colorscheme/str
 *          else return str
 *  2) One initial / => return str+1
 *  3) Two initial /'s =>
 *       a) If colorscheme is defined and not "X11", return /colorscheme/(str+2)
 *       b) else return (str+2)
 *  4) Two /'s, not both initial => return str.
 *
 * Note that 1), 2), and 3b) allow the default X11 color scheme. 
 *
 * In other words,
 *   xxx => /colorscheme/xxx     if colorscheme is defined and not "X11"
 *   xxx => xxx                  otherwise
 *   /xxx => xxx
 *   /X11/yyy => yyy
 *   /xxx/yyy => /xxx/yyy
 *   //yyy => /colorscheme/yyy   if colorscheme is defined and not "X11"
 *   //yyy => yyy                otherwise
 * 
 * At present, no other error checking is done. For example, 
 * yyy could be "". This will be caught later.
 */

#define DFLT_SCHEME "X11/"      /* Must have final '/' */
#define DFLT_SCHEME_LEN ((sizeof(DFLT_SCHEME)-1)/sizeof(char))
#define ISNONDFLT(s) ((s) && *(s) && strncasecmp(DFLT_SCHEME, s, DFLT_SCHEME_LEN-1))

static char* resolveColor (char* str)
{
    char* s;
    char* ss;   /* second slash */
    char* c2;   /* second char */

    if (!strcmp(str, "black")) return str;
    if (!strcmp(str, "white")) return str;
    if (!strcmp(str, "lightgrey")) return str;
    if (*str == '/') {   /* if begins with '/' */
	c2 = str+1;
        if ((ss = strchr(c2, '/'))) {  /* if has second '/' */
	    if (*c2 == '/') {    /* if second '/' is second character */
		    /* Do not compare against final '/' */
		if (ISNONDFLT(colorscheme))
		    s = fullColor (colorscheme, c2+1);
		else
		    s = c2+1;
	    }
	    else if (strncasecmp(DFLT_SCHEME, c2, DFLT_SCHEME_LEN)) s = str;
	    else s = ss + 1;
	}
	else s = c2;
    }
    else if (ISNONDFLT(colorscheme)) s = fullColor (colorscheme, str);
    else s = str;
    return canontoken(s);
}

int colorxlate(char *str, gvcolor_t * color, color_type_t target_type)
{
    static hsvrgbacolor_t *last;
    static char *canon;
    static size_t allocated;
    char *p, *q;
    hsvrgbacolor_t fake;
    char c;
    double H, S, V, A, R, G, B;
    double C, M, Y, K;
    unsigned int r, g, b, a;
    size_t len;
    int rc;

    color->type = target_type;

    rc = COLOR_OK;
    for (; *str == ' '; str++);	/* skip over any leading whitespace */
    p = str;

    /* test for rgb value such as: "#ff0000"
       or rgba value such as "#ff000080" */
    a = 255;			/* default alpha channel value=opaque in case not supplied */
    if (*p == '#' && sscanf(p, "#%2x%2x%2x%2x", &r, &g, &b, &a) >= 3) {
	switch (target_type) {
	case HSVA_DOUBLE:
	    R = (double) r / 255.0;
	    G = (double) g / 255.0;
	    B = (double) b / 255.0;
	    A = (double) a / 255.0;
	    rgb2hsv(R, G, B, &H, &S, &V);
	    color->u.HSVA[0] = H;
	    color->u.HSVA[1] = S;
	    color->u.HSVA[2] = V;
	    color->u.HSVA[3] = A;
	    break;
	case RGBA_BYTE:
	    color->u.rgba[0] = (unsigned char)r;
	    color->u.rgba[1] = (unsigned char)g;
	    color->u.rgba[2] = (unsigned char)b;
	    color->u.rgba[3] = (unsigned char)a;
	    break;
	case CMYK_BYTE:
	    R = (double) r / 255.0;
	    G = (double) g / 255.0;
	    B = (double) b / 255.0;
	    rgb2cmyk(R, G, B, &C, &M, &Y, &K);
	    color->u.cmyk[0] = (int) C *255;
	    color->u.cmyk[1] = (int) M *255;
	    color->u.cmyk[2] = (int) Y *255;
	    color->u.cmyk[3] = (int) K *255;
	    break;
	case RGBA_WORD:
	    color->u.rrggbbaa[0] = (int)(r * 65535 / 255);
	    color->u.rrggbbaa[1] = (int)(g * 65535 / 255);
	    color->u.rrggbbaa[2] = (int)(b * 65535 / 255);
	    color->u.rrggbbaa[3] = (int)(a * 65535 / 255);
	    break;
	case RGBA_DOUBLE:
	    color->u.RGBA[0] = (double) r / 255.0;
	    color->u.RGBA[1] = (double) g / 255.0;
	    color->u.RGBA[2] = (double) b / 255.0;
	    color->u.RGBA[3] = (double) a / 255.0;
	    break;
	case COLOR_STRING:
	    break;
	case COLOR_INDEX:
	    break;
	default:
	    UNREACHABLE();
	}
	return rc;
    }

    /* test for hsv value such as: ".6,.5,.3" */
    if ((c = *p) == '.' || isdigit(c)) {
	len = strlen(p);
	if (len >= allocated) {
	    allocated = len + 1 + 10;
	    canon = grealloc(canon, allocated);
	}
	q = canon;
	while ((c = *p++)) {
	    if (c == ',')
		c = ' ';
	    *q++ = c;
	}
	*q = '\0';

	if (sscanf(canon, "%lf%lf%lf", &H, &S, &V) == 3) {
	    /* clip to reasonable values */
	    H = fmax(fmin(H, 1.0), 0.0);
	    S = fmax(fmin(S, 1.0), 0.0);
	    V = fmax(fmin(V, 1.0), 0.0);
	    switch (target_type) {
	    case HSVA_DOUBLE:
		color->u.HSVA[0] = H;
		color->u.HSVA[1] = S;
		color->u.HSVA[2] = V;
		color->u.HSVA[3] = 1.0; /* opaque */
		break;
	    case RGBA_BYTE:
		hsv2rgb(H, S, V, &R, &G, &B);
		color->u.rgba[0] = (int) (R * 255);
		color->u.rgba[1] = (int) (G * 255);
		color->u.rgba[2] = (int) (B * 255);
		color->u.rgba[3] = 255;	/* opaque */
		break;
	    case CMYK_BYTE:
		hsv2rgb(H, S, V, &R, &G, &B);
		rgb2cmyk(R, G, B, &C, &M, &Y, &K);
		color->u.cmyk[0] = (int) C *255;
		color->u.cmyk[1] = (int) M *255;
		color->u.cmyk[2] = (int) Y *255;
		color->u.cmyk[3] = (int) K *255;
		break;
	    case RGBA_WORD:
		hsv2rgb(H, S, V, &R, &G, &B);
		color->u.rrggbbaa[0] = (int) (R * 65535);
		color->u.rrggbbaa[1] = (int) (G * 65535);
		color->u.rrggbbaa[2] = (int) (B * 65535);
		color->u.rrggbbaa[3] = 65535;	/* opaque */
		break;
	    case RGBA_DOUBLE:
		hsv2rgb(H, S, V, &R, &G, &B);
		color->u.RGBA[0] = R;
		color->u.RGBA[1] = G;
		color->u.RGBA[2] = B;
		color->u.RGBA[3] = 1.0;	/* opaque */
		break;
	    case COLOR_STRING:
		break;
	    case COLOR_INDEX:
		break;
	    default:
		UNREACHABLE();
	    }
	    return rc;
	}
    }

    /* test for known color name (generic, not renderer specific known names) */
    fake.name = resolveColor(str);
    if (!fake.name)
	return COLOR_MALLOC_FAIL;
    if (last == NULL || strcmp(last->name, fake.name)) {
	last = bsearch(&fake, color_lib, sizeof(color_lib) / sizeof(hsvrgbacolor_t),
	               sizeof(fake), colorcmpf);
    }
    if (last != NULL) {
	switch (target_type) {
	case HSVA_DOUBLE:
	    color->u.HSVA[0] = ((double) last->h) / 255.0;
	    color->u.HSVA[1] = ((double) last->s) / 255.0;
	    color->u.HSVA[2] = ((double) last->v) / 255.0;
	    color->u.HSVA[3] = ((double) last->a) / 255.0;
	    break;
	case RGBA_BYTE:
	    color->u.rgba[0] = last->r;
	    color->u.rgba[1] = last->g;
	    color->u.rgba[2] = last->b;
	    color->u.rgba[3] = last->a;
	    break;
	case CMYK_BYTE:
	    R = last->r / 255.0;
	    G = last->g / 255.0;
	    B = last->b / 255.0;
	    rgb2cmyk(R, G, B, &C, &M, &Y, &K);
	    color->u.cmyk[0] = (int) C * 255;
	    color->u.cmyk[1] = (int) M * 255;
	    color->u.cmyk[2] = (int) Y * 255;
	    color->u.cmyk[3] = (int) K * 255;
	    break;
	case RGBA_WORD:
	    color->u.rrggbbaa[0] = last->r * 65535 / 255;
	    color->u.rrggbbaa[1] = last->g * 65535 / 255;
	    color->u.rrggbbaa[2] = last->b * 65535 / 255;
	    color->u.rrggbbaa[3] = last->a * 65535 / 255;
	    break;
	case RGBA_DOUBLE:
	    color->u.RGBA[0] = last->r / 255.0;
	    color->u.RGBA[1] = last->g / 255.0;
	    color->u.RGBA[2] = last->b / 255.0;
	    color->u.RGBA[3] = last->a / 255.0;
	    break;
	case COLOR_STRING:
	    break;
	case COLOR_INDEX:
	    break;
	default:
	    UNREACHABLE();
	}
	return rc;
    }

    /* if we're still here then we failed to find a valid color spec */
    rc = COLOR_UNKNOWN;
    switch (target_type) {
    case HSVA_DOUBLE:
	color->u.HSVA[0] = color->u.HSVA[1] = color->u.HSVA[2] = 0.0;
	color->u.HSVA[3] = 1.0; /* opaque */
	break;
    case RGBA_BYTE:
	color->u.rgba[0] = color->u.rgba[1] = color->u.rgba[2] = 0;
	color->u.rgba[3] = 255;	/* opaque */
	break;
    case CMYK_BYTE:
	color->u.cmyk[0] =
	    color->u.cmyk[1] = color->u.cmyk[2] = color->u.cmyk[3] = 0;
	break;
    case RGBA_WORD:
	color->u.rrggbbaa[0] = color->u.rrggbbaa[1] = color->u.rrggbbaa[2] = 0;
	color->u.rrggbbaa[3] = 65535;	/* opaque */
	break;
    case RGBA_DOUBLE:
	color->u.RGBA[0] = color->u.RGBA[1] = color->u.RGBA[2] = 0.0;
	color->u.RGBA[3] = 1.0;	/* opaque */
	break;
    case COLOR_STRING:
	break;
    case COLOR_INDEX:
	break;
    default:
	UNREACHABLE();
    }
    return rc;
}

static void rgba_wordToByte (int* rrggbbaa, unsigned char* rgba)
{
    int i;

    for (i = 0; i < 4; i++) {
	rgba[i] = rrggbbaa[i] * 255 / 65535;
    }
}

static void rgba_dblToByte (double* RGBA, unsigned char* rgba)
{
    int i;

    for (i = 0; i < 4; i++) {
	rgba[i] = (unsigned char)(RGBA[i] * 255);
    }
}

/* colorCvt:
 * Color format converter. 
 * Except for the trivial case, it converts the input color to a string
 * representation and then calls colorxlate.
 * ncolor must point to a gvcolor_t struct with type specifying the desired
 * output type.
 */
int colorCvt(gvcolor_t *ocolor, gvcolor_t *ncolor)
{
    int rc;
    char buf[BUFSIZ];
    char* s;
    unsigned char rgba[4];

    if (ocolor->type == ncolor->type) {
	memcpy (&ncolor->u, &ocolor->u, sizeof(ocolor->u));    
	return COLOR_OK; 
    }
    s = buf;
    switch (ocolor->type) {
    case HSVA_DOUBLE :
	snprintf(buf, sizeof(buf), "%.03f %.03f %.03f %.03f",
	    ocolor->u.HSVA[0], ocolor->u.HSVA[1], ocolor->u.HSVA[2], ocolor->u.HSVA[3]);
	break;
    case RGBA_BYTE :
	snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
	    ocolor->u.rgba[0], ocolor->u.rgba[1], ocolor->u.rgba[2], ocolor->u.rgba[3]);
	break;
    case RGBA_WORD:
	rgba_wordToByte (ocolor->u.rrggbbaa, rgba);
	snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", rgba[0], rgba[1], rgba[2],
	         rgba[3]);
	break;
    case RGBA_DOUBLE:
	rgba_dblToByte (ocolor->u.RGBA, rgba);
	snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", rgba[0], rgba[1], rgba[2],
	         rgba[3]);
	break;
    case COLOR_STRING:
	s = ocolor->u.string;
	break;
    case CMYK_BYTE :
	return COLOR_UNKNOWN;
	break;
    case COLOR_INDEX:
	return COLOR_UNKNOWN;
	break;
    default:
	return COLOR_UNKNOWN;
	break;
    }
    rc = colorxlate (s, ncolor, ncolor->type);
    return rc;
}

/* setColorScheme:
 * Set current color scheme for resolving names.
 */
void setColorScheme (char* s)
{
    colorscheme = s;
}