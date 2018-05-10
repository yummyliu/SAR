```c
/*
 * event types for protocol handler
 */
typedef enum {
    SBUF_EV_READ,       /* got new packet */
    SBUF_EV_RECV_FAILED,    /* error */
    SBUF_EV_SEND_FAILED,    /* error */
    SBUF_EV_CONNECT_FAILED, /* error */
    SBUF_EV_CONNECT_OK, /* got connection */
    SBUF_EV_FLUSH,      /* data is sent, buffer empty */
    SBUF_EV_PKT_CALLBACK,   /* next part of pkt data */
    SBUF_EV_TLS_READY   /* TLS was established */
} SBufEvent;
```

