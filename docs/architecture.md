# Lume Static Server Architecture

Lume is a small Linux C static web server. The current implementation focuses on
complete business behavior, clear module boundaries, reliable resource cleanup,
and phase-two non-blocking I/O hardening. Performance-oriented changes such as
sendfile and edge-triggered epoll are intentionally left for later iterations.

## Request Flow

1. `main` loads `config/server.conf` and initializes the listening socket.
2. `server` creates a non-blocking TCP socket, binds it to `PORT`, and starts
   listening.
3. `event_loop` creates an epoll instance, registers the listener, accepts new
   clients, enforces `MAX_CONNECTIONS`, dispatches client events, and prunes
   idle connections after `REQUEST_TIMEOUT`.
4. `connection` reads until the HTTP request headers are complete, rejects
   oversized requests, builds one response, writes headers and files in explicit
   states, and closes the short-lived TCP connection.
5. `http` parses the request line and builds standard HTTP/1.1 response
   headers.
6. `static_file` maps a URI into `ROOT_DIR`, rejects unsafe paths, opens the
   requested regular file, and returns metadata for the response.
7. `mime` maps file extensions to response `Content-Type` values.

## Security Model

The static file layer decodes percent-encoded paths, rejects malformed encodings,
control characters, backslashes, and `..` path segments, then validates the final
`realpath` result is still inside the configured document root. Symlinks that
escape the root are rejected.

## Current Scope

The current server supports:

- HTTP `GET`
- Status codes `200`, `400`, `404`, and `501`
- Short connections with `Connection: close`
- Static HTML, CSS, JS, JSON, text, and common image MIME types
- Configurable request header size, active connection, and idle timeout limits

The connection state machine is:

- `READING`
- `PROCESSING`
- `WRITING_HEADER`
- `WRITING_FILE`
- `CLOSING`

The current server does not support:

- TLS
- keep-alive
- directory listings
- CGI or dynamic handlers
- cache validation headers

## Iteration Notes

The next performance iteration should keep the module boundaries intact and
focus on `sendfile`, buffer reuse, response construction cost, and
benchmark-driven epoll trigger changes.
