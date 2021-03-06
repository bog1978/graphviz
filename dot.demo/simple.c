/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include <graphviz/gvc.h>
#include <stddef.h>

int main(int argc, char **argv)
{
    GVC_t *gvc;
    graph_t *g;
    FILE *fp;

    gvc = gvContext();

    if (argc > 1)
	fp = fopen(argv[1], "r");
    else
	fp = stdin;
    g = agread(fp, NULL);

    gvLayout(gvc, g, "dot");

    gvRender(gvc, g, "plain", stdout);

    gvFreeLayout(gvc, g);

    agclose(g);

    return (gvFreeContext(gvc));
}
