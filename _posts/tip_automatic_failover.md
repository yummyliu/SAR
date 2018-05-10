介绍

基于内部流复制的PostgreSQL集群，PAF（PostgreSQL Automatic Failover）可以向Pacemaker报告当前服务节点的状态（start stop master slave catching up …）；当主机宕机了，找到最近的一个slave，将其promote。

Fencing

将一个node隔离在cluster之外；当master不再响应cluster的信息时；有效的Fencing，避免脑裂或者数据损坏；