/* The Diary Server client: HTTP/1.0 over a plain TCP connection. */

#ifndef SERVER_H
#define SERVER_H

#include <glib.h>

typedef struct Server Server;

Server *ServerNew(const char *address, const char *key);   /* host or host:port */
void ServerFree(Server *server);
const char *ServerAddress(Server *server);

/* Sends a request (path like "/entries/2026-11-05", body and base may be
   NULL). Returns the HTTP status, or 0 if there was no answer; then *error
   says why. *text gets the reply as UTF-8, *version its X-Diary-Version
   (NULL if none); free both with g_free. */
int ServerRequest(Server *server, const char *method, const char *path,
                  const char *body, const char *base,
                  char **text, char **version, char **error);

GArray *DaysInReply(const char *text);          /* lines that start with a date */
char *ReasonForStatus(int status, const char *text);

#endif
