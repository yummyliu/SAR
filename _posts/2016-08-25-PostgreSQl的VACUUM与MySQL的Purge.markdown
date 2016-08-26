---
layout: post
title: PostgreSQl的VACUUM与MySQL的Purge
date: 2016-08-25 09:26
header-img: "img/head.jpg"
categories: jekyll update
---

In InnoDB, only the most recent version of an updated row is retained in the table itself.  
Old versions of updated rows are moved to the rollback segment, while deleted row versions are left in place and marked for future cleanup.  
Thus, purge must get rid of any deleted rows from the table itself, and clear out any old versions of updated rows from the rollback segment.  
All the information necessary to find the deleted records that might need to be purged is also written to the rollback segment, so it's quite easy to find the rows that need to be cleaned out; 
and the old versions of the updated records are all in the rollback segment itself, so those are easy to find, too.  
One small downside of this approach is that performing an update means writing two tuples - the old one must be copied to the undo tablespace, and the new one must be written in its place.

PostgreSQL takes a completely different approach.  
There is no rollback tablespace, or anything similar.  
When a row is updated, the old version is left in place; the new version is simply written into the table along with it.  
As in InnoDB, records deleted are marked for future cleanup, but without also writing a record to the rollback tablespace.  
Both of these differences result in slightly less work when the operation is initially performed, but the payback is the eventual cleanup is more expensive.  
Lacking a centralized record of what must be purged, PostgreSQL's VACUUM has historically needed to scan the entire table to look for records that might require cleanup.  
Beginning in PostgreSQL 8.3, there is an optimization called HOT (for "heap only tuple") which allows some vacuuming to be done on the fly in single-page increments; beginning in PostgreSQL 8.4 and higher, the system maintains a bitmap, called the visibility map, which indicates which pages of the table might possibly contain tuples in need of cleanup, and VACUUM can scan only those pages.  
However, a full scan of each index is still required during each VACUUM, make it still a somewhat expensive operation for large tables.

Since both systems can potentially store multiple versions of any given tuple, both can lead to "bloat", where the size of a table grows far beyond the amount of data actually stored in it, the bloat shows up in different places.  
Under InnoDB, most of the bloat (with the exception of any not-yet-removable deleted rows) is in the rollback tablespace, whereas in PostgreSQL it's mixed in with the actual table data.  
In either case, the problem occurs mostly (a) in the presence of long-running transactions or (b) when VACUUM and/or purge can't keep up with the rate at which old row versions are accumulating and must be removed.  
Beginning in PostgreSQL 8.3, VACUUM is typically performed automatically in the background, using multiple (three by default) concurrent worker processes.   
MySQL performs purges in the background, but it is single-threaded.  
Percona Server, and possibly other MySQL forks, offer multiple purge threads.
