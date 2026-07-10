#ifndef LUME_EVENT_LOOP_H
#define LUME_EVENT_LOOP_H

typedef enum lume_event_type {
    LUME_EVENT_LISTENER = 1,
    LUME_EVENT_CONNECTION = 2
} lume_event_type;

typedef struct lume_event_source {
    lume_event_type type;
} lume_event_source;

struct lume_server;

int lume_event_loop_run(struct lume_server *server);

#endif
