---
layout: post
title: SaltStack安装与测试
date: 2018-05-30 15:13
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - Devops
---

## 安装

系统环境

```
cat /etc/centos-release
CentOS Linux release 7.2.1511 (Core)
```

1. import saltstack repository key

   ```
   rpm --import https://repo.saltstack.com/yum/redhat/7/x86_64/latest/SALTSTACK-GPG-KEY.pub
   ```

2. repo

   ```bash
   cat > /etc/yum.repos.d/saltstack.repo <<-'EOF'
   [saltstack-repo]
   name=SaltStack repo for RHEL/CentOS $releasever
   baseurl=https://repo.saltstack.com/yum/redhat/$releasever/$basearch/latest
   enabled=1
   gpgcheck=1
   gpgkey=https://repo.saltstack.com/yum/redhat/$releasever/$basearch/latest/SALTSTACK-GPG-KEY.pub
   EOF

   sudo yum clean expire-cache

   # sudo yum update # 暂时不升级系统和包的版本
   ```

3. yum install 

   ```bash
   yum install -y salt-master
   yum install -y salt-minion
   yum install -y salt-ssh
   yum install -y salt-syndic
   yum install -y salt-cloud
   ```

4. start salt-master

   ```bash
   systemctl start salt-master.service
   ```

5. start salt-minion

   ```bash
   systemctl start salt-minion.service
   ```

## 配置

大部分使用默认设置；

1. 让minion知道master,设置master选项；可以支持master选项动态设置，那么此时的master_type必须设置为func；另外还有一个master_type可以为failover，此时master选项必须设置为一个list；

   ```bash
   sed -i 's/^#master:.*$/master: 10.9.50.127/g' /etc/salt/minion
   ```

2. 使用salt-key管理master上的密钥：配置完成后，如果master要管理minion就要接受minion的key

   ```bash
   <root@DBC ~>$ cd /etc/salt/pki/master/minions_pre
   <root@DBC minions_pre>$ ll
   total 36
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-118-32
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-136-45
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-144-141
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-167-124
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-178-80
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-187-51
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-197-232
   -rw-r--r-- 1 root root 450 Mar 27 15:32 10-9-97-214
   -rw-r--r-- 1 root root 450 Mar 27 15:33 liuyangming.dev.p1staff.com
   <root@DBC minions_pre>$ salt-key -A
   The following keys are going to be accepted:
   Unaccepted Keys:
   10-9-118-32
   10-9-136-45
   10-9-144-141
   10-9-167-124
   10-9-178-80
   10-9-187-51
   10-9-197-232
   10-9-97-214
   liuyangming.dev.p1staff.com
   Proceed? [n/Y] Y
   Key for minion 10-9-118-32 accepted.
   Key for minion 10-9-136-45 accepted.
   Key for minion 10-9-144-141 accepted.
   Key for minion 10-9-167-124 accepted.
   Key for minion 10-9-178-80 accepted.
   Key for minion 10-9-187-51 accepted.
   Key for minion 10-9-197-232 accepted.
   Key for minion 10-9-97-214 accepted.
   Key for minion liuyangming.dev.p1staff.com accepted.
   ```

3. 测试

   ```bash
   <root@DBC minions_pre>$ salt '*' test.ping
   liuyangming.dev.p1staff.com:
       True
   10-9-167-124:
       True
   10-9-97-214:
       True
   10-9-197-232:
       True
   10-9-178-80:
       True
   10-9-136-45:
       True
   10-9-118-32:
       True
   10-9-144-141:
       True
   10-9-187-51:
       True
   ```
