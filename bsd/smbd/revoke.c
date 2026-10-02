/* Keep this signal callback in resident text: the interrupted worker can
 * have either overlay mapped. Do not call overlay code from this handler. */
#include "smbd.h"
#include <sys/socket.h>

int session_connection_fd = -1;
volatile int session_revoked;

void
session_revoke(sig)
int sig;
{
 (void)sig; session_revoked = 1;
 /* Interrupt idle reads without closing/reusing the descriptor. Normal
  * serve cleanup releases handles, delete-on-close state and the session. */
 if (session_connection_fd >= 0) shutdown(session_connection_fd, 2);
}
