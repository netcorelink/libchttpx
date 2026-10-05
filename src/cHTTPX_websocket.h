/**
 * Copyright (c) 2026 netcorelink
 *
 * Distributed under the BSD 3-Clause License. See LICENSE for details.
 */

#ifndef CHTTPX_WEBSOCKET_H
#define CHTTPX_WEBSOCKET_H

#include "cHTTPX_serv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** WebSocket continuation frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
/** WebSocket text frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
/** WebSocket binary frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
/** WebSocket close frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
/** WebSocket ping frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
/** WebSocket pong frame opcode. */
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

/**
 * Parsed WebSocket frame metadata and payload.
 *
 * Used by legacy or diagnostic paths that expose a decoded frame.
 */
typedef struct
{
    int fin;
    int opcode;
    int masked;
    uint64_t payload_len;
    unsigned char mask[4];
    unsigned char* payload;
} wsocket_frame_t;

/**
 * Active WebSocket connection handle.
 *
 * Created by the HTTP/2 Extended CONNECT bridge. Application code registers
 * message and close callbacks, then sends frames through the public helpers.
 */
typedef struct chttpx_wsocket
{
    chttpx_socket_t socket;
    int connected;
    int opcode;
    chttpx_request_t* request;
    void* user_data;
    void* _internal;
} chttpx_wsocket_t;

/**
 * Callback invoked for a complete text or binary message.
 *
 * @param wsocket Active WebSocket connection.
 * @param data Message payload bytes.
 * @param len Payload length in bytes.
 */
typedef void (*chttpx_wsocket_handler_t)(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Callback invoked when the peer closes or the connection is aborted.
 *
 * @param wsocket Active WebSocket connection.
 * @param code Close status code.
 * @param reason Optional UTF-8 close reason.
 * @param len Reason length in bytes.
 */
typedef void (*chttpx_wsocket_close_handler_t)(chttpx_wsocket_t* wsocket, uint16_t code, const unsigned char* reason, size_t len);

/**
 * Route entry callback invoked after a successful WebSocket handshake.
 *
 * @param wsocket Newly accepted WebSocket connection.
 */
typedef void (*chttpx_wsocket_route_t)(chttpx_wsocket_t* wsocket);

/**
 * Register a WebSocket route on a router path prefix.
 *
 * @param router Target router.
 * @param path Path relative to the router prefix.
 * @param handler Callback invoked after handshake success.
 */
void cHTTPX_WSocketRegisterRoute(chttpx_router_t* router, const char* path, chttpx_wsocket_route_t handler);

/**
 * Set the message handler for an active WebSocket.
 *
 * @param wsocket Active WebSocket connection.
 * @param handler Callback for complete text or binary messages.
 */
void cHTTPX_WSocketOnMessage(chttpx_wsocket_t* wsocket, chttpx_wsocket_handler_t handler);

/**
 * Set the close handler for an active WebSocket.
 *
 * @param wsocket Active WebSocket connection.
 * @param handler Callback for peer close or abrupt abort.
 */
void cHTTPX_WSocketOnClose(chttpx_wsocket_t* wsocket, chttpx_wsocket_close_handler_t handler);

/**
 * Attach opaque application data to a WebSocket.
 *
 * @param wsocket Active WebSocket connection.
 * @param user_data Pointer stored on the connection.
 */
void cHTTPX_WSocketSetData(chttpx_wsocket_t* wsocket, void* user_data);

/**
 * Return opaque application data previously attached to a WebSocket.
 *
 * @param wsocket Active WebSocket connection.
 * @return Stored user data pointer, or NULL.
 */
void* cHTTPX_WSocketData(chttpx_wsocket_t* wsocket);

/**
 * Send a text WebSocket frame.
 *
 * @param wsocket Active WebSocket connection.
 * @param data UTF-8 payload bytes.
 * @param len Payload length in bytes.
 * @return Zero on success or a negative error code.
 */
int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Send a binary WebSocket frame.
 *
 * @param wsocket Active WebSocket connection.
 * @param data Binary payload bytes.
 * @param len Payload length in bytes.
 * @return Zero on success or a negative error code.
 */
int cHTTPX_WSocketSendBinary(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Send a WebSocket ping frame.
 *
 * @param wsocket Active WebSocket connection.
 * @param data Optional ping payload.
 * @param len Payload length in bytes (at most 125).
 * @return Zero on success or a negative error code.
 */
int cHTTPX_WSocketPing(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Send a WebSocket close frame and mark the connection closing.
 *
 * @param wsocket Active WebSocket connection.
 * @param code Close status code, or 0 for 1000.
 * @param reason Optional UTF-8 close reason.
 * @return Zero on success or a negative error code.
 */
int cHTTPX_WSocketClose(chttpx_wsocket_t* wsocket, uint16_t code, const char* reason);

/**
 * Legacy HTTP/1.1 upgrade helper retained for source compatibility.
 *
 * Always returns unavailable: WebSockets are served over HTTP/2 Extended CONNECT.
 *
 * @param client_socket Unused client socket.
 * @param sec_wsocket_key Unused Sec-WebSocket-Key value.
 * @return Always cHTTPX_ERR_UNAVAILABLE.
 */
int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key);

/**
 * Legacy synchronous receive helper retained for source compatibility.
 *
 * @param wsocket Unused connection handle.
 * @param buffer Unused output buffer.
 * @param len Unused buffer capacity.
 * @return Always cHTTPX_ERR_UNAVAILABLE.
 */
int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len);

/**
 * Transport callback used by the HTTP/2 bridge to emit framed bytes.
 *
 * @param context Opaque transport context.
 * @param data Encoded WebSocket frame bytes.
 * @param len Frame length in bytes.
 * @param end_stream Whether the DATA frame should end the stream.
 * @return Zero on success or a negative error code.
 */
typedef int (*chttpx_wsocket_transport_send_fn)(void* context, const unsigned char* data, size_t len, bool end_stream);

/**
 * Look up a registered WebSocket route handler for a request path.
 *
 * @param server Owning server instance.
 * @param path Request path, optionally with a query string.
 * @return Matching route handler, or NULL when none is registered.
 */
chttpx_wsocket_route_t _chttpx_websocket_find_route(chttpx_serv_t* server, const char* path);

/**
 * Allocate a WebSocket handle bound to an HTTP/2 transport send callback.
 *
 * @param server Owning server instance.
 * @param socket Client socket associated with the connection.
 * @param request Handshake request metadata.
 * @param transport_send Callback that writes encoded frames.
 * @param transport_context Opaque context for transport_send.
 * @return New WebSocket handle, or NULL on allocation failure.
 */
chttpx_wsocket_t* _chttpx_websocket_create(chttpx_serv_t* server, chttpx_socket_t socket, chttpx_request_t* request,
                                           chttpx_wsocket_transport_send_fn transport_send, void* transport_context);

/**
 * Mark a WebSocket connection as ready for application traffic.
 *
 * @param wsocket WebSocket handle created by the HTTP/2 bridge.
 */
void _chttpx_websocket_mark_connected(chttpx_wsocket_t* wsocket);

/**
 * Feed inbound WebSocket bytes into the frame parser.
 *
 * @param wsocket Active WebSocket connection.
 * @param data Newly received bytes.
 * @param len Number of bytes in data.
 * @return Zero on success or a negative protocol/error code.
 */
int _chttpx_websocket_feed(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Destroy a WebSocket handle and release internal buffers.
 *
 * @param wsocket WebSocket handle to free.
 */
void _chttpx_websocket_destroy(chttpx_wsocket_t* wsocket);

/**
 * Free all WebSocket routes registered on a server.
 *
 * @param server Server whose websocket_state should be released.
 */
void _chttpx_websocket_server_cleanup(chttpx_serv_t* server);

#ifdef __cplusplus
}
#endif

#endif
