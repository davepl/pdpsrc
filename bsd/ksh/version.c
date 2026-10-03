/*	$NetBSD: version.c,v 1.5 2005/06/26 19:09:00 christos Exp $	*/

/*
 * value of $KSH_VERSION (or $SH_VERSION)
 */

#if !defined(lint) && defined(HAVE_SYS_CDEFS_H)
#include <sys/cdefs.h>
__RCSID("$NetBSD: version.c,v 1.5 2005/06/26 19:09:00 christos Exp $");
#endif


#include "sh.h"

char ksh_version [] =
	"@(#)PD KSH v5.2.14 99/07/13.2";
