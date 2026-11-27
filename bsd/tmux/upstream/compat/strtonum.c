/* $Id: strtonum.c,v 1.1 2007-10-31 14:26:26 nicm Exp $ */
/*	$OpenBSD: strtonum.c,v 1.6 2004/08/03 19:38:01 millert Exp $	*/

/*
 * Copyright (c) 2004 Ted Unangst and Todd Miller
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "tmux.h"

#define INVALID 	1
#define TOOSMALL 	2
#define TOOLARGE 	3

tmux_num
strtonum(const char *numstr, tmux_num minval, tmux_num maxval,
    const char **errstrp)
{
	tmux_num ll = 0;
	char *ep;
	int error = 0;
	struct errval {
		const char *errstr;
		int err;
	} ev[4];

	ev[0].errstr = NULL;
	ev[0].err = errno;
	ev[INVALID].errstr = "invalid";
	ev[INVALID].err = EINVAL;
	ev[TOOSMALL].errstr = "too small";
	ev[TOOSMALL].err = ERANGE;
	ev[TOOLARGE].errstr = "too large";
	ev[TOOLARGE].err = ERANGE;
	errno = 0;
	if (minval > maxval)
		error = INVALID;
	else {
		ll = TMUX_NUM_PARSE(numstr, &ep, 10);
		if (numstr == ep || *ep != '\0')
			error = INVALID;
		else if ((ll == TMUX_NUM_MIN && errno == ERANGE) || ll < minval)
			error = TOOSMALL;
		else if ((ll == TMUX_NUM_MAX && errno == ERANGE) || ll > maxval)
			error = TOOLARGE;
	}
	if (errstrp != NULL)
		*errstrp = ev[error].errstr;
	errno = ev[error].err;
	if (error)
		ll = 0;

	return (ll);
}
