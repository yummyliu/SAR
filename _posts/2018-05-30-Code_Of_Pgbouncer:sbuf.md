---
layout: post
title: 
date: 2018-04-10 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
    - Pgbouncer
---

# Sbuf

```c
/*
 * Stream Buffer.
 *
 * Stream is divided to packets.  On each packet start
 * protocol handler is called that decides what to do.
 */
struct SBuf {
    struct event ev;    /* libevent handle */

    uint8_t wait_type;  /* track wait state */
    uint8_t pkt_action; /* method for handling current pkt */
    uint8_t tls_state;  /* progress of tls */

    int sock;       /* fd for this socket */

    unsigned pkt_remain;    /* total packet length remaining */
    sbuf_cb_t proto_cb; /* protocol callback */

    SBuf *dst;      /* target SBuf for current packet */

    IOBuf *io;      /* data buffer, lazily allocated */

    const SBufIO *ops;  /* normal vs. TLS */
    struct tls *tls;    /* TLS context */
    const char *tls_host;   /* target hostname */
};
```



##### wait_type

```c
/* PgSocket.wait_type */
enum WType {
    W_NONE = 0,
    W_SOCK,
    W_TIME
};
```

##### pkt_action

```c
#define ACT_UNSET 0
#define ACT_SEND 1
#define ACT_SKIP 2
#define ACT_CALL 3
```

##### tls_state

```c
enum TLSState {
    SBUF_TLS_NONE,
    SBUF_TLS_DO_HANDSHAKE,
    SBUF_TLS_IN_HANDSHAKE,
    SBUF_TLS_OK,
};
```



###### sbuf_main_loop

循环接收-解析-发送；接收packet，放在stream buf里，根据不同的情况，执行proto_cb；处理这个packet；

proto_cb就两个：

+ client_proto
+ server_proto ()

