/*
 *  C implementation of the Aho-Corasick pattern matching algorithm. It's based
 *  on ScannerDaemon's Java version by Kurt Huwig and
 *  http://www-sr.informatik.uni-tuebingen.de/~buehler/AC/AC.html
 *  Thanks to Kurt Huwig for pointing me to this page.
 *
 *  Copyright (C) 2002 Tomasz Kojm <zolw@konarski.edu.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "clamav.h"
#include "others.h"
#include "matcher.h"
#include "unrarlib.h"
#include "defaults.h"

int cli_addpatt(struct cl_node *root, struct patt *pattern)
{
	struct cl_node *pos, *next;
	int i;

    if(pattern->length < CL_MIN_LENGTH) {
	return CL_EPATSHORT;
    }

    pos = root;

    for(i = 0; i < CL_MIN_LENGTH; i++) {
	next = pos->trans[((unsigned char) pattern->pattern[i]) & 0xff]; 

	if(!next) {
	    next = (struct cl_node *) cli_calloc(1, sizeof(struct cl_node));
	    if(!next)
		return CL_EMEM;

	    root->nodes++;
	    root->nodetable = (struct cl_node **) realloc(root->nodetable, (root->nodes) * sizeof(struct cl_node *));
	    root->nodetable[root->nodes - 1] = next;

	    pos->trans[((unsigned char) pattern->pattern[i]) & 0xff] = next;
	}

	pos = next;
    }

    pos->islast = 1;

    pattern->next = pos->list;
    pos->list = pattern;

    return 0;
}

void cli_enqueue(struct nodelist **bfs, struct cl_node *n)
{
	struct nodelist *new;

    new = (struct nodelist *) cli_calloc(1, sizeof(struct nodelist));

    new->next = *bfs;
    new->node = n;
    *bfs = new;
}

struct cl_node *cli_dequeue(struct nodelist **bfs)
{
	struct nodelist *handler, *prev = NULL;
	struct cl_node *pt;

    handler = *bfs;

    while(handler && handler->next) {
	prev = handler;
	handler = handler->next;
    }

    if(!handler) {
	return NULL;
    } else {
	pt = handler->node;
	free(handler);
	if(prev)
	    prev->next = NULL;
	else
	    *bfs = NULL;

	return pt;
    }
}

void cli_maketrans(struct cl_node *root)
{
	struct nodelist *bfs = NULL;
	struct cl_node *child, *node;
	int i;


    root->fail = NULL;
    cli_enqueue(&bfs, root);

    while((node = cli_dequeue(&bfs))) {
	if(node->islast)
	    continue;

	for(i = 0; i < CL_NUM_CHILDS; i++) {
	    child = node->trans[i];
	    if(!child) {
		if(node->fail)
		    node->trans[i] = (node->fail)->trans[i];
		else
		    node->trans[i] = root;
	    } else {
		if(node->fail)
		    child->fail = (node->fail)->trans[i];
		else
		    child->fail = root;

		cli_enqueue(&bfs, child);
	    }
	}
    }
}

void cl_buildtrie(struct cl_node *root)
{
    cli_maketrans(root);
}

void cli_freepatt(struct patt *list)
{
	struct patt *handler, *prev;


    handler = list;

    while(handler) {
	free(handler->pattern);
	free(handler->virname);
	prev = handler;
	handler = handler->next;
	free(prev);
    }
}

void cl_freetrie(struct cl_node *root)
{
	int i;

    for(i = 0; i < root->nodes; i++) {
	cli_freepatt(root->nodetable[i]->list);
	free(root->nodetable[i]);
    }

    free(root->nodetable);
    free(root);
}

int cl_scanbuff(const char *buffer, unsigned int length, char **virname, const struct cl_node *root)
{
	struct cl_node *current;
	struct patt *pt;
	int i, position, *partcnt;

    current = (struct cl_node *) root;

    partcnt = (int *) cli_calloc(root->partsigs + 1, sizeof(int));

    for(i = 0; i < length; i++)  {
	current = current->trans[(unsigned char) buffer[i] & 0xff];

	if(current->islast) {
	    position = i - CL_MIN_LENGTH + 1;

	    pt = current->list;
	    while(pt) {
		if(cli_findpos(buffer, position, length, pt)) {
		    if(pt->sigid) { /* it's a partial signature */
			if(partcnt[pt->sigid] + 1 == pt->partno) {
			    if(++partcnt[pt->sigid] == pt->parts) { /* last */
				if(virname)
				    *virname = pt->virname;
				free(partcnt);
				return CL_VIRUS;
			    }
			}
		    } else { /* old type signature */
			if(virname)
			    *virname = pt->virname;
			free(partcnt);
			return CL_VIRUS;
		    }
		}

		pt = pt->next;
	    }

	    current = current->fail;
	}
    }

    free(partcnt);
    return CL_CLEAN;
}

int cli_findpos(const char *buffer, int offset, int length, const struct patt *pattern)
{
	int bufferpos = offset + CL_MIN_LENGTH;
	int postfixend = offset + length;
	int i;


    for(i = CL_MIN_LENGTH; i < pattern->length; i++) {

	bufferpos %= length;

	if(bufferpos == postfixend)
	    return 0;

	if(pattern->pattern[i] != CLI_IGN && (char) pattern->pattern[i] != buffer[bufferpos])
	    return 0;

	bufferpos++;
    }

    return 1;
}
