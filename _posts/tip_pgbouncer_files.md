# 文件结构

| admin.c     | show等管理操作                            |
| ----------- | ------------------------------------ |
| client.c    | 客户端连接handle                          |
| dnslookup.c | 基于c-are等库的dns查询                      |
| hba.c       | auth_type为hba时，该文件用来加载hba文件          |
| janitor.c   | 连接池的定期维护操作                           |
| loader.c    | 加载pgbouncer.ini和auth_file文件          |
| main.c      | 入口                                   |
| objects.c   | 维护pgbouncer的内部对象，各种list和cache以及tree等 |
| pam.c       | auth_type的类型为pam时用的                  |
| pktbuf.c    | packet buffer 的数据包的发送和接收             |
| pooler.c    | 连接池socket监听的处理handle                 |
| proto.c     | 协议头部信息处理                             |
| sbuf.c      | 流缓冲区                                 |
| server.c    | db server连接handle                    |
| stats.c     | pgbouncer自身的统计信息，show stats相关        |
| system.c    | libc不提供的 自己实现的系统函数                   |
| takeover.c  | 一个进程接管另一个进程                          |
| util.c      | 一些工具函数                               |
| varcache.c  | 服务配置参数的值，连接上的编码，timezone等信息          |

+ 内部对象

  + user_list：never free
  + database_list: never free
  + pool_list: never free
  + user_tree: auth_file中的user
  + pam_user_tree: pam认证方式的user
  + login_client_list: pam
  + server_cache
  + client_cache
  + db_cache
  + pool_cache
  + user_cache
  + iobuf_cache
  + autodatabase_idle_list

+ 数据结构

  + statlist

    带统计信息的双向链表

  + aatree

    简单的红黑树

  + stab

    基于链表，存储一些预先分配的对象


## SBUF

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

## callback from Sbuf

```
server_proto
```

| signal                 | description                 | handle                                    |
| ---------------------- | --------------------------- | ----------------------------------------- |
| SBUF_EV_RECV_FAILED    | error                       | disconnect_server                         |
| SBUF_EV_SEND_FAILED    | error                       | disconnect_client                         |
| SBUF_EV_READ           | got new packet              | handle_server_startup;handle_server_work; |
| SBUF_EV_CONNECT_FAILED | error                       | disconnect_server                         |
| SBUF_EV_CONNECT_OK     | got connection              | handle_connect                            |
| SBUF_EV_FLUSH          | data is sent， buffer empty | release_server                            |
| SBUF_EV_PKT_CALLBACK   | next part of pkt data       |                                           |
| SBUF_EV_TLS_READY      | TLS was established         |                                           |

## SocketState

## IOBUF

```c
/*
 * 0 .. done_pos         -- sent
 * done_pos .. parse_pos -- parsed, to send
 * parse_pos .. recv_pos -- received, to parse
 */
struct iobuf {
    unsigned done_pos;
    unsigned parse_pos;
    unsigned recv_pos;
    uint8_t buf[FLEX_ARRAY];
};
```

