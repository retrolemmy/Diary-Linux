#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include "dates.h"
#include "server.h"
#include "store.h"

#define kDefaultPort 8642
#define kTimeout     10                 /* seconds, for connecting and each read */
#define kMaxReply    (16 * 1024 * 1024)

struct Server {
    char *address;                      /* as typed */
    char *host;                         /* without the port */
    char *key;
};

Server *ServerNew(const char *address, const char *key)
{
    Server *server = g_new0(Server, 1);
    const char *colon;

    server->address = g_strstrip(g_strdup(address));
    colon = strrchr(server->address, ':');
    server->host = colon != NULL && strchr(server->address, ':') == colon
                   ? g_strndup(server->address, colon - server->address)
                   : g_strdup(server->address);
    server->key = g_strdup(key != NULL ? key : "");
    return server;
}

void ServerFree(Server *server)
{
    if (server == NULL)
        return;
    g_free(server->address);
    g_free(server->host);
    g_free(server->key);
    g_free(server);
}

const char *ServerAddress(Server *server)
{
    return server->address;
}

static char *ConnectError(Server *server, GError *error)
{
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED))
        return g_strdup_printf("The server refused the connection. Is Diary Server running on %s?",
                               server->host);
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))
        return g_strdup_printf("The server didn't answer: %s", server->host);
    if (error->domain == G_RESOLVER_ERROR)
        return g_strdup_printf("Can't find the server %s", server->host);
    return g_strdup_printf("Can't connect to the server %s (%s)", server->host, error->message);
}

/* Splits a raw reply into status, version and body. */
static int ParseReply(GByteArray *reply, char **text, char **version)
{
    char *data = (char *)reply->data, *end = NULL, *line;
    gsize i, length = reply->len;
    int status;

    for (i = 0; i + 3 < length; i++)
        if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
            end = data + i;
            break;
        }
    if (end == NULL || strncmp(data, "HTTP/", 5) != 0)
        return 0;
    *end = 0;

    line = strchr(data, ' ');
    if (line == NULL)
        return 0;
    status = atoi(line + 1);

    for (line = strchr(data, '\n'); line != NULL; line = strchr(line, '\n')) {
        line++;
        if (version != NULL && g_ascii_strncasecmp(line, "X-Diary-Version:", 16) == 0) {
            const char *v = line + 16;
            gsize n = 0;
            while (*v == ' ')
                v++;
            while (v[n] > ' ')
                n++;
            *version = g_strndup(v, n);
        }
    }

    if (text != NULL)
        *text = DecodeText(end + 4, length - (end + 4 - data));
    return status;
}

int ServerRequest(Server *server, const char *method, const char *path,
                  const char *body, const char *base,
                  char **text, char **version, char **error)
{
    GSocketClient *client;
    GSocketConnection *connection;
    GInputStream *input;
    GError *failure = NULL;
    GString *request;
    GByteArray *reply;
    guint8 buffer[16384];
    gssize got;
    gboolean ok;
    int status = 0;

    if (text != NULL)
        *text = NULL;
    if (version != NULL)
        *version = NULL;
    if (error != NULL)
        *error = NULL;

    request = g_string_new(NULL);
    g_string_append_printf(request, "%s %s%scharset=utf-8 HTTP/1.0\r\n"
                           "Host: %s\r\nUser-Agent: Diary for Linux\r\n",
                           method, path, strchr(path, '?') != NULL ? "&" : "?", server->address);
    if (server->key[0] != 0)
        g_string_append_printf(request, "X-Diary-Key: %s\r\n", server->key);
    if (base != NULL)
        g_string_append_printf(request, "X-Diary-Base: %s\r\n", base);
    if (body != NULL)
        g_string_append_printf(request, "Content-Type: text/plain; charset=utf-8\r\n"
                               "Content-Length: %lu\r\n", (unsigned long)strlen(body));
    g_string_append(request, "\r\n");
    if (body != NULL)
        g_string_append(request, body);

    client = g_socket_client_new();
    g_socket_client_set_timeout(client, kTimeout);
    connection = g_socket_client_connect_to_host(client, server->address, kDefaultPort, NULL, &failure);
    g_object_unref(client);
    if (connection == NULL) {
        if (error != NULL)
            *error = ConnectError(server, failure);
        g_error_free(failure);
        g_string_free(request, TRUE);
        return 0;
    }

    ok = g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                   request->str, request->len, NULL, NULL, NULL);
    g_string_free(request, TRUE);

    reply = g_byte_array_new();
    input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    while (ok && (got = g_input_stream_read(input, buffer, sizeof(buffer), NULL, NULL)) != 0) {
        if (got < 0 || reply->len + got > kMaxReply)
            ok = FALSE;
        else
            g_byte_array_append(reply, buffer, got);
    }
    g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
    g_object_unref(connection);

    if (!ok) {
        if (error != NULL)
            *error = g_strdup("The connection to the server broke off.");
    } else {
        g_byte_array_append(reply, (const guint8 *)"", 1);  /* for strchr */
        reply->len--;
        status = ParseReply(reply, text, version);
        if (status == 0 && error != NULL)
            *error = g_strdup("The server's reply wasn't understood. Is it a Diary Server?");
    }
    g_byte_array_free(reply, TRUE);
    if (status == 0 && text != NULL) {
        g_free(*text);
        *text = NULL;
    }
    return status;
}

GArray *DaysInReply(const char *text)
{
    GArray *days = NewDays();
    char name[11];
    int day;

    while (text != NULL && *text != 0) {
        const char *next = strchr(text, '\n');

        if (strlen(text) >= 10) {
            memcpy(name, text, 10);
            name[10] = 0;
            if (ParseEntryName(name, &day))
                g_array_append_val(days, day);
        }
        text = next != NULL ? next + 1 : NULL;
    }
    return days;
}

char *ReasonForStatus(int status, const char *text)
{
    char *reason;

    if (status == 403)
        return g_strdup("The server wants a different key.");
    reason = g_strstrip(g_strdup(text != NULL ? text : ""));
    if (*reason == 0) {
        g_free(reason);
        reason = g_strdup_printf("The server answered with error %d.", status);
    }
    return reason;
}
