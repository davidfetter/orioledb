# OrioleDB – building a modern cloud-native storage engine
(... and solving some PostgreSQL wicked problems)

[![build status](https://github.com/orioledb/orioledb/actions/workflows/build.yml/badge.svg)](https://github.com/orioledb/orioledb/actions)
[![codecov](https://codecov.io/gh/orioledb/orioledb/branch/main/graph/badge.svg?token=shh4jn0DUK)](https://codecov.io/gh/orioledb/orioledb)

OrioleDB is a new storage engine for PostgreSQL, bringing a modern approach to
database capacity, capabilities and performance to the world's most-loved
database platform.

OrioleDB consists of an extension, building on the innovative table access
method framework and other standard Postgres extension interfaces. By extending
and enhancing the current table access methods, OrioleDB opens the door to
a future of more powerful storage models that are optimized for cloud and
modern hardware architectures.

OrioleDB is currently distributed under the standard PostgreSQL license.

1. Designed for modern hardware.  OrioleDB design avoids legacy CPU bottlenecks
   on modern servers containing dozens and hundreds CPU cores, providing
   optimized usage of modern storage technologies such as SSD and NVRAM.

2. Reduced maintenance needs.  OrioleDB implements the concepts of undo log
   and page-mergins, eliminating the need for dedicated garbage collection
   processes.  Additionally, OrioleDB implements default 64-bit transaction
   identifiers, thus eliminating the well-known and painful wraparound problem.

3. Designed to be distributed.  OrioleDB implements a row-level write-ahead
   log with support for parallel apply.  This log architecture is optimized
   for raft consensus-based replication allowing the implementation of
   active-active multimaster.

The key technical differentiations of OrioleDB are as follows:

1. No buffer mapping and lock-less page reading.  In-memory pages in OrioleDB
   are connected with direct links to the storage pages.  This eliminates the
   need for in-buffer mapping along with its related bottlenecks. Additionally,
   in OrioleDB in-memory page reading doesn't involve atomic operations.
   Together, these design decisions bring vertical scalability for Postgres
   to the whole new level.

2. MVCC is based on the UNDO log concept.  In OrioleDB, old versions of tuples
   do not cause bloat in the main storage system, but eviction into the undo
   log comprising undo chains.  Page-level undo records allow the system
   to easily reclaim space occupied by deleted tuples as soon as possible.
   Together with page-mergins, these mechanisms eliminate bloat in the majority
   of cases.  Dedicated VACUUMing of tables is not needed as well, removing
   a significant and common cause of system performance deterioration and
   database outages.

3. Copy-on-write checkpoints and row-level WAL.  OrioleDB utilizes
   copy-on-write checkpoints, which provides a structurally consistent snapshot
   of data every moment of time.  This is friendly for modern SSDs and allows
   row-level WAL logging.  In turn, row-level WAL logging is easy to
   parallelize (done), compact and suitable for active-active
   multimaster (planned).

See, [usage](doc/usage.md) and [architecture](doc/arch.md) documentation
as well as [PostgresBuild 2021 slides](https://www.slideshare.net/AlexanderKorotkov/solving-postgresql-wicked-problems).

## Status

OrioleDB now has public alpha status.  It is recommended for experiments,
testing, benchmarking, etc., but is not recommended for production usage.
If you are interested in OrioleDB's benefits in production, please
[contact us](mailto:sales@orioledb.com).

## Installation

Before building and installing OrioleDB, one should ensure to have the following:

 * [PostgreSQL 14 with extensibility patches](https://github.com/orioledb/postgres),
 * Development package of libzstd,
 * python 3.5+ with testgres package.

Typical installation procedure may look like this:

```bash
 $ git clone https://github.com/orioledb/orioledb
 $ cd orioledb
 $ make USE_PGXS=1
 $ make USE_PGXS=1 install
 $ make USE_PGXS=1 installcheck
```

Before starting working with OrioleDB, adding the following line to
`postgresql.conf` is required.  This change requires a restart of
the PostgreSQL database server.

```
shared_preload_libraries = 'orioledb.so'
```

And also run the following SQL-query on the database.

```sql
CREATE EXTENSION orioledb;
```

Once the above steps are complete, you can start using OrioleDB's tables.
See [usage](doc/usage.md) documentation for details.

```sql
CREATE TABLE table_name (...) USING orioledb;
```

## License

OrioleDB is released under the
[PostgreSQL License](https://opensource.org/licenses/postgresql),
a liberal Open Source license, similar to the BSD or MIT licenses.

OrioleDB extension for PostgresSQL

Copyright © 2021-2022, Oriole DB Inc.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement
is hereby granted, provided that the above copyright notice and this paragraph
and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.

THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS,
AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE,
SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.