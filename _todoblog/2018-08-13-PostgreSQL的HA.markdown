---
layout: post
title: 初识PostgreSQL的High Availability
date: 2018-08-13 11:04
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

# HA简介

> Anything that can go wrong will go wrong. 
>
> ​					——Murphy's law

常说的几个9的可用性，就是（uptime/(uptime+downtime)）；

## 故障检测

关于故障检测我们需要考虑若干问题：

+ 主是否只是暂时阻塞了？
+ 判断错误之后，是否可以两个master同时运行？
+ 集群被分割成两部分，这两部分不可通，但是都和app可通，怎么办？
+ 如何避免重复failover？
+ 系统对于timeout如何处理的？

故障时的自动故障转移（failover），常用的是两个工具**pacemaker**和**Linux HA**。

## 脑裂

集群的可写主节点通常只有一个，由于某些原因导致集群中出现两个主节点，这时就需要quorum和fencing的技术。

### 仲裁（quorum） 

当集群被分割成两个互不相通的组时，需要一种机制决定那个组是最终的master，这就叫仲裁；比如加入另一个决策节点（tiebreaker ），但注意这有可能成为一个单点。

### 围栏（fencing）

仲裁只是failover的第一步，决定了哪个组是最终的master后，对于另一组，需要确保将其与现有环境隔离，或者说保证另一组彻底down掉；通常我们对这个过程称作：STONITH （shoot the other node in the head ）。有很多fencing机制，最有效的就是直接控制目标机器将其关闭。

## Linux HA技术栈

+ 消息传输：Corosync，通过 corosync-keygen 生成的一个共享key进行交流；
+ 机器管理：Pacemaker，基于Corosync进行仲裁，最终对集群进行failover操作。我们并不是告诉Pacemaker怎么做，而是告诉Pacemaker这个集群应该是什么样子？比如，应该启动哪些服务，这些服务在哪以及以什么顺序启动当出现故障时，Pacemaker重新制定计划其进行操作。
+ 应用适配器脚本：resource agents ，基于Open Cluster Framework (OCF) 编写的agent（通常就是shell脚本），对需要HA的资源的配置以及查询相关信息，官方站点有PostgreSQL的resource agents（PG RA）。
+ fencing适配器：fence agents ，对底层fencing命令的包装，比如，网络远程停止机器或通过hypervisor 重置VM等。
+ 配置以上模块用到的客户端：pcs，底层的配置都是在XML中写好的，直接改配置即可，但是这样不够友好。



# HA方案

## 方案0. Keep a DBA on duty

DBA 24*7 守在DB身旁，以备不测。。。；

## 方案1. keepalived-repmger

###### 图1 keepalived-repmger中的故障切换逻辑

![image-20180813111059882](/image/image-20180813111059882.png)

当master故障时，keepalived将vip切换到一个hot standby上。并且该hot standby的VRRP协议中的角色切换为master，并自动启动notify_master脚本，将这个hot standby提升为master。

为了防止脑裂，集群中必须有一个 **shared witness server** 。其做出决定并将故障转移到较高优先级的从上。**witness server**确保某一网段有较高优先级，这样其他server不会自己promote。

## 方案2. HAproxy-Pgbouncer

###### 图2 Haproxy-Pgbouncer的故障切换逻辑

![image-20180813112113584](/image/image-20180813112113584.png)

通过这个架构，可以做负载均衡，提高整体带宽，资源利用率和响应时间，避免单一节点过载。通过冗余提高可用性和可靠性。

这个方案中，当前端haproxy1挂掉后，Keepalived将vip迁移到haproxy2上。而当后端master挂掉后，repmgrd(replicaltion manager watch-dog)将hot standby提升为主。这一方案中同样加上shared witness server同样有意义。

## 方案3. Pacemaker

### 简介

![image-20180829103926035](/image/image-20180829103926035.png)

PaceMaker分为几部分：

###### 消息层：Heartbeat/Corosync

+ 节点的关系，以及节点的添加删除的周知；
+ 节点间消息传递
+ 仲裁系统

###### 集群资源管理器：PaceMaker

+ 存储集群的配置
+ 基于消息层，实现最大资源利用率
+ 扩展性：按照指定接口编写好相应的脚本，就能被PaceMaker管理。

###### 集群胶水工具

+ 除了传输消息（Corosync）和CRM（PaceMaker）之外的工具
+ 节点本地的与packmaker的资源代理器交互的资源管理器
+ 提供fencing功能的STONITH守护进程

###### 资源代理器

+ 管理集群资源的代理器
+ 提供一些管理操作：start/stop/monitor/promote/demote等
+ 有一些现成的代理器，如：Apache，PostgreSQL，drbd等

Pacemaker使用hostname辨别各个系统，可以使用DNS或者直接在/etc/hosts下配置（最好配置的短小精悍，比如：pros-db12）；不要在hostname中，使用primary/slave，这在failover后混淆。

### 配置

配置一个简单的HA，需要两个DB节点，一个quorum节点，以及一个cluster IP，pacemake将其作为primary的secondaryIP；

```
tdb1: 10.9.167.124
tdb2: 10.9.118.32
quorum: 10.9.136.45
cluster IP: 10.9.167.125
```



##### 主机网络配置

```bash
hostnamectl set-hostname db1
hostnamectl set-hostname db2
hostnamectl set-hostname quorum1
# 配置/etc/hosts

echo "10.9.167.124 db1" >> /etc/hosts
echo "10.9.118.32 db2" >> /etc/hosts
echo "10.9.136.45 quorum1" >> /etc/hosts
```

##### 安装工具

```bash
yum install -y pacemaker pcs
```

##### 启动pcsd服务

```
# 设置pcs自带的用户：hacluster用户的密码
passwd hacluster
```

```
# 启动
ready (3)> systemctl start pcsd.service
ready (3)> pgrep pcsd
10.9.167.124 : 3475
10.9.136.45  : 3231
10.9.118.32  : 3229
```

##### 集群认证

```
[root@db1 ~]# pcs cluster auth db1 db2 quorum1
Username: hacluster
Password:
db1: Authorized
db2: Authorized
quorum1: Authorized
```

##### 集群设置

```
[root@db1 ~]# pcs cluster setup --name pgcluster db1 db2 quorum1
Destroying cluster on nodes: db1, db2, quorum1...
db1: Stopping Cluster (pacemaker)...
db2: Stopping Cluster (pacemaker)...
quorum1: Stopping Cluster (pacemaker)...
db1: Successfully destroyed cluster
quorum1: Successfully destroyed cluster
db2: Successfully destroyed cluster

Sending 'pacemaker_remote authkey' to 'db1', 'db2', 'quorum1'
db1: successful distribution of the file 'pacemaker_remote authkey'
db2: successful distribution of the file 'pacemaker_remote authkey'
quorum1: successful distribution of the file 'pacemaker_remote authkey'
Sending cluster config files to the nodes...
db1: Succeeded
db2: Succeeded
quorum1: Succeeded

Synchronizing pcsd certificates on nodes db1, db2, quorum1...
db1: Success
db2: Success
quorum1: Success
Restarting pcsd on the nodes in order to reload the certificates...
db1: Success
db2: Success
quorum1: Success
```

##### 启动集群

```
[root@db1 ~]# pcs cluster start --all
db1: Starting Cluster...
db2: Starting Cluster...
quorum1: Starting Cluster...
[root@db1 ~]# pcs status corosync

Membership information
----------------------
    Nodeid      Votes Name
         2          1 db2
         3          1 quorum1
         1          1 db1 (local)
```

##### 安装并配置PostgreSQL

```bash
sh pg_install.sh 10
# 检查若干参数，默认都可
listen_addresses
wal_level
max_wal_senders
wal_keep_segments
# max_replication_slots
hot_standby

# 创建用户PostgreSQL Resource Agent的文件夹
mkdir ~/run
# 创建replication用户
CREATE ROLE replication login replication encrypted password '123456';

# 配置pg_hba.conf for standby replica
host  replication     replication    10.9.0.0/16        md5

# 本地PostgreSQL resource agent访问
local postgres       postgres        trust
```

##### 同步备库

```
pg_basebackup --pgdata=/var/lib/pgsql/10/data -X f --host=db1 --username=replication --checkpoint=fast -P
```

##### 集群全局配置

在集群的任一节点进行配置，首先关闭fence

```
pcs property set stonith-enabled=false
```

默认地，Packmaker允许资源在任何节点运行，在DBHA中，将这个参数关闭。

```
pcs property set symmetric-cluster=false
```

##### 集群资源配置

```bash
pcs resource create pgsql ocf:heartbeat:pgsql \
      pgctl="/usr/pgsql-10/bin/pg_ctl" \
      psql="/usr/pgsql-10/bin/psql" \
      pgdata="/var/lib/pgsql/10/data" \
      logfile="/var/lib/pgsql/10/startup.log" \
      config="/var/lib/pgsql/10/data/postgresql.conf" \
      tmpdir="/var/lib/pgsql/10/run/" \
      rep_mode="sync" \
      node_list="db1 db2" \
      master_ip="10.9.167.125" \
      repuser="replication" \
      primary_conninfo_opt="password=123456" \
      stop_escalate="110" \
      \
      op start timeout=120s on-fail=restart \
      op stop timeout=120s on-fail=fence \
      op monitor interval=3s timeout=10s on-fail=restart \
      op monitor interval=2s role=Master timeout=10s on-fail=fence \
      op promote timeout=120s on-fail=block \
      op demote timeout=120s on-fail=fence \
      meta migration-threshold=2 \
      master clone-max=2 clone-node-max=1
```

###### 分解解释

```bash
pcs resource create pgsql ocf:heartbeat:pgsql \
# 创建了一个名为pgsql的资源，将使用ocf代理中的heartbeat文件夹下的pgsql代理，下面是关于这些代理的配置
      pgctl="/usr/pgsql-10/bin/pg_ctl" \
      psql="/usr/pgsql-10/bin/psql" \
# pgsql的安装目录
      pgdata="/var/lib/pgsql/10/data" \
      logfile="/var/lib/pgsql/10/startup.log" \
      config="/var/lib/pgsql/10/data/postgresql.conf" \
      tmpdir="/var/lib/pgsql/10/run/" \
# pgsql相关的文件夹
      rep_mode="sync" \
      node_list="db1 db2" \
# 告诉pgsql使用同步复制
      master_ip="10.9.167.125" \
      repuser="replication" \
      primary_conninfo_opt="password=123456" \
# 周知集群主节点的信息，从而知道从哪复制数据，注意这里使用的master_ip就是上面的cluster ip
      stop_escalate="110" \
# Number of shutdown retries (using -m fast) before resorting to -m immediate
# 资源代理多次尝试关闭
      \
      op start timeout=120s on-fail=restart \
      op stop timeout=120s on-fail=fence \
      op monitor interval=3s timeout=10s on-fail=restart \
      op monitor interval=2s role=Master timeout=10s on-fail=fence \
      op promote timeout=120s on-fail=block \
      op demote timeout=120s on-fail=fence \
      meta migration-threshold=2 \
      master clone-max=2 clone-node-max=1
```

通过`pcs resource describe ocf:heartbeat:pgsql `可以看到相应agent的参数。通过以上配置，我们告诉PaceMaker集群是包含两个节点的主从，其中在各个节点上运行相应的实例。

##### cluster IP的HA

```bash
 pcs resource create master-ip ocf:heartbeat:IPaddr2 \
      ip="10.9.167.125" iflabel="master" \
      op monitor interval=5s
```

##### 配置约束

###### masterIP约束

```bash
pcs constraint colocation add master-ip with master pgsql-master
```

###### 顺序约束

只有将cluster IP转移到新master节点上后，才promote新的master；创建这个约束，也意味着创建了一个对称的约束：在停止master ip之前，demote老的pgsql-master

```
pcs constraint order start master-ip then promote pgsql-master
```

###### 位置约束

将pgsql和master-ip的指定到相应的节点上，这里节点的score都是0，意味着让PaceMaker来修改。

> ```bash
> [root@db1 ~]# pcs constraint location pgsql prefers db1=0 db2=0
> Error: pgsql is a master/slave resource, you should use the master id: pgsql-master when adding constraints. Use --force to override.
> ```

```
   pcs constraint location pgsql-master prefers db1=0 db2=0
   pcs constraint location master-ip prefers db1=0 db2=0
```

##### 设置fencing

###### 下载fencing agent

```bash
yum install fence-agents-ipmilan -y
```

###### 创建资源

```bash
pcs stonith create db1-fence fence_ipmilan \
      auth=md5 ipaddr=... passwd=... lanplus=1 login=root method=cycle \
      pcmk_host_check=static-list \
      pcmk_host_list=db1
pcs stonith create db2-fence fence_ipmilan \
      auth=md5 ipaddr=... passwd=... lanplus=1 login=root method=cycle \
      pcmk_host_check=static-list \
      pcmk_host_list=db2
```

###### 位置约束

```
 pcs constraint location db1-fence prefers db2=0 quorum1=0
 pcs constraint location db2-fence prefers db1=0 quorum1=0
```

###### 在PaceMaker中注册fence方法

```
pcs stonith level add 1 db1 db1-fence
pcs stonith level add 1 db2 db2-fence
```

###### 打开集群的fence开关

```
pcs property set stonith-enabled=true
```

##### 验证

```bash
[root@db1 ~]# crm_mon -1A
Stack: corosync
Current DC: quorum1 (version 1.1.18-11.el7_5.3-2b07d5c5a9) - partition with quorum
Last updated: Wed Aug 29 18:25:58 2018
Last change: Wed Aug 29 18:22:29 2018 by root via cibadmin on db1

3 nodes configured
5 resources configured

Online: [ db1 db2 quorum1 ]

Active resources:

 Master/Slave Set: pgsql-master [pgsql]
     pgsql      (ocf::heartbeat:pgsql): Slave db2 (Monitoring)

Node Attributes:
* Node db1:
    + master-pgsql                      : -INFINITY
    + pgsql-data-status                 : LATEST
    + pgsql-status                      : STOP
* Node db2:
    + master-pgsql                      : -INFINITY
    + pgsql-data-status                 : DISCONNECT
    + pgsql-status                      : HS:alone
* Node quorum1:

Failed Actions:
* db1-fence_start_0 on quorum1 'unknown error' (1): call=25, status=Error, exitreason='',
    last-rc-change='Wed Aug 29 18:17:48 2018', queued=0ms, exec=2117ms
* db2-fence_start_0 on quorum1 'unknown error' (1): call=27, status=Error, exitreason='',
    last-rc-change='Wed Aug 29 18:17:50 2018', queued=0ms, exec=1118ms
* pgsql_monitor_3000 on db1 'unknown error' (1): call=24, status=complete, exitreason='Failed to show my xlog location.',
    last-rc-change='Wed Aug 29 17:53:33 2018', queued=0ms, exec=252ms
* db2-fence_start_0 on db1 'unknown error' (1): call=35, status=Error, exitreason='',
    last-rc-change='Wed Aug 29 18:17:48 2018', queued=0ms, exec=2140ms
* db1-fence_start_0 on db2 'unknown error' (1): call=27, status=Error, exitreason='',
    last-rc-change='Wed Aug 29 18:17:46 2018', queued=0ms, exec=2136ms
```















### 参考文献

https://www.pgcon.org/2013/schedule/attachments/279_PostgreSQL_9_and_Linux_HA.pdf





