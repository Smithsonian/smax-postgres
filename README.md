![Build Status](https://github.com/Smithsonian/smax-postgres/actions/workflows/build.yml/badge.svg)
![Static Analysis](https://github.com/Smithsonian/smax-postgres/actions/workflows/analyze.yml/badge.svg)
<a href="https://smithsonian.github.io/smax-postgres/apidoc/html/files.html">
 ![API documentation](https://github.com/Smithsonian/smax-postgres/actions/workflows/dox.yml/badge.svg)
</a>
<a href="https://smithsonian.github.io/smax-postgres/index.html">
 ![Project page](https://github.com/Smithsonian/smax-postgres/actions/workflows/pages/pages-build-deployment/badge.svg)
</a>

<picture>
  <source srcset="resources/CfA-logo-dark.png" alt="CfA logo" media="(prefers-color-scheme: dark)"/>
  <source srcset="resources/CfA-logo.png" alt="CfA logo" media="(prefers-color-scheme: light)"/>
  <img src="resources/CfA-logo.png" alt="CfA logo" width="400" height="67" align="right"/>
</picture>
<br clear="all">


# smax-postgres

Record [SMA-X](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
history in PostgreSQL / TimescaleDB. It is free to use, in any way you like, without licensing restrictions.

 - [API documentation](https://smithsonian.github.io/smax-postgres/apidoc/html/files.html)
 - [Project pages](https://smithsonian.github.io/smax-postgres) on github.io

Author: Attila Kovacs

Last Updated: 18 September 2024

----------------------------------------------------------------------------------------------------------------------

## Table of Contents

 - [Introduction](#introduction)
 - [Prerequisites](#prerequisites)
 - [Building `smax-postgres`](#building)
 - [Installation](#installation)
 - [Database organization (for clients)](#database-organization)
 - [Configuration reference](#configuration-reference)

----------------------------------------------------------------------------------------------------------------------

<a name="introduction"></a>
## Introduction

`smax-postgres` is a daemon application, which can collect data from an 
[SMA information eXchange (SMA-X)](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
realtime database and insert these into a __PostgreSQL__ database to create a time-series historical record for all or 
selected SMA-X variables. The program is highly customizable and supports both regular updates for changing variables 
as well as regular snapshots of all selected SMA-X variables.



<a name="prerequisites"></a>
## Prerequisites

The `smax-postgres` application has build and runtime dependencies on the following software:

 - __PostgreSQL__ installation and development files (`libpq.so` and `lipq.fe.h`).
 - [Smithsonian/smax-clib](https://github.com/Smithsonian/smax-clib)
 - [Smithsonian/redisx](https://github.com/Smithsonian/redisx)
 - [Smithsonian/xchange](https://github.com/Smithsonian/xchange)
 - __Popt__ development libraries (`libpopt-dev`in Debian, or `popt-devel` in RPM distros)
 - (_optional_) __TimescaleDB__ extensions.
 - (_optional_) __systemd__ development files (`libsystemd.so` and `sd-daemon.h`).

Additionally, to configure your SMA-X server, you will need the 
[Smithsonian/smax-server](https://github.com/Smithsonian/smax-server) repo also.

----------------------------------------------------------------------------------------------------------------------

<a name="building"></a>
## Building `smax-postgres`

You can configure the build, either by editing `config.mk` or else by defining the relevant environment variables 
prior to invoking `make`. The following build variables can be configured:

 - `PGDIR`: Root directory of a specific PostgreSQL installation to build against (not set by default). If not set
   we'll build against the default PostgreSQL available on your system.
  
 - `SYSTEMD`: Sets whether to compile with SystemD integration (needs `libsystemd.so` and `sd-daemon.h`). Default
   is 1 (enabled).
   
 - `CC`: The C compiler to use (default: `gcc`).

 - `CPPFLAGS`: C preprocessor flags, such as externally defined compiler constants.
 
 - `CFLAGS`: Flags to pass onto the C compiler (default: `-g -Os -Wall`). Note, `-Iinclude` will be added 
   automatically.
   
 - `CSTANDARD`: Optionally, specify the C standard to compile for, e.g. `c99` to compile for the C99 standard. If
   defined then `-std=$(CSTANDARD)` is added to `CFLAGS` automatically.
   
 - `WEXTRA`: If set to 1, `-Wextra` is added to `CFLAGS` automatically.
   
 - `LDFLAGS`: Extra linker flags (default: _not set_). Note, `-lm -pthread -lsmax -lredisx -lxchange -lpq -lpopt` will 
   be added automatically.

 - `CHECKEXTRA`: Extra options to pass to `cppcheck` for the `make check` target
 
 - `XCHANGE`: If the [Smithsonian/xchange](https://github.com/Smithsonian/xchange) library is not installed on your
   system (e.g. under `/usr`) set `XCHANGE` to where the distribution can be found. The build will expect to find 
   `xchange.h` under `$(XCHANGE)/include` and `libxchange.so` / `libxchange.a` under `$(XCHANGE)/lib` or else in the 
   default `LD_LIBRARY_PATH`.
 
 - `REDISX`: If the [Smithsonian/redisx](https://github.com/Smithsonian/redisx) library is not installed on your
   system (e.g. under `/usr`) set `REDISX` to where the distribution can be found. The build will expect to find 
   `redisx.h` under `$(REDISX)/include` and `libredisx.so` / `libredisx.a` under `$(REDISX)/lib` or else in the 
   default `LD_LIBRARY_PATH`.
   
 - `SMAXLIB`: If the [Smithsonian/smax-clib](https://github.com/Smithsonian/smax-clib) library is not installed on 
   your system (e.g. under `/usr`) set `SMAXLIB` to where the distribution can be found. The build will expect to find 
   `smax.h` under `$(SMAXLIB)/include` and `libsmax.so` / `libsmax.a` under `$(SMAXLIB)/lib` or else in the default 
   `LD_LIBRARY_PATH`.
 
After configuring, you can simply run `make`, which will build `bin/smax-postgres`, and user documentation. You may 
also build other `make` target(s). (You can use `make help` to get a summary of the available `make` targets). 

Now you may compile `smax-postgres`:

```bash
  $ make
```

After building the library you can install the above components to the desired locations on your system. For a 
system-wide install you may simply run:

```bash
  $ sudo make install
```

Or, to install in some other locations, you may set a prefix and/or `DESTDIR`. For example, to install under `/opt` 
instead, you can:

```bash
  $ sudo make prefix="/opt" install
```

Or, to stage the installation (to `/usr`) under a 'build root':

```bash
  $ make DESTDIR="/tmp/stage" install
```

----------------------------------------------------------------------------------------------------------------------

<a name="installation"></a>
## Installation

Prior to installation, you should check that the PostgreSQL service name is correct in `smax-postgres.service`, and
edit it as necessary for your system configuration. You may also edit the `cfg/smax-postgres.cfg` now, or after it
is installed.

Provided the build was successful, you can install the executables, configuration files, and optionally the SystemD 
unit files via:

```bash
  $ sudo make install
```

(When installing at the SMA, you may want `make install-sma` instead, to install with SMA-specific configuration).
In case of SystemD integration you should also reload the SystemD daemon so `smax-postgres.service` can be enabled 
and managed as desired:

```bash
  $ sudo systemd daemon-reload
```

After that you can start the service as:

```bash
  $ sudo systemd start smax-postgres
```

### Staging / advanced installation

By default, `make install` will install the `smax-postgress` executable to `/usr/bin`, configuration under `/etc/`,
SystemD service unit under `/etc/systemd/system`, and documentation under `/usr/share/doc/smax-postgres/`. Instead of
`/usr`, you may want to install into another destination, such as `/opt/` or `/usr/local`. You can do that by setting
the `DESTDIR` environment prior to `make install`, e.g.:

```bash
  $ export DESTDIR="/opt"
```

Additionally, you can also stage the installation under a different root, by setting the `PREFIX` environment variable,
e.g.:

```bash
  $ export PREFIX="~/rmpbuild/BUILD/smax-postgres"
```

### Standard error/output with SystemD integration

In case of SystemD integration, errors will get logged to the journal, and can be investigated by `journalctl`. E.g. to
see the errors in the last 3 hours, you may:

```bash
  $ journalctl -u smax-postgres --since "3 hours ago"
```

Standard output is logged to the file `/var/log/smax-postgres.out` for the current session. Restarting `smax-postgres` will 
start a new file. (Because of buffering, there may be a long lag before you'll see stuff appear in this log file.)



### Initial setup of the SQL database

Prior to using `smax-postgres`, you will need to configure users and access privileges (roles) for the database, and may 
want to create the database instance manually. You will need to create a user designated for the `smax-postgres` program, 
and specify its credentials in the `smax-postgres` configuration file. This user will not require `CREATEDB` permission, 
but it will need permission to create tables in the existing database and to insert data or to search the tables
(i.e. read/write privileges). Additionally, you may also create the designated database instance assigned to whatever 
user to own. If you create the database manually, do not forget to set the name of the designated database in the 
`smax-postgres` configuration file.

Normally `smax-postgres` will assume that the database to use has been fully set up, including a 'titles' table 
(containing 2 columns: _text_ variable IDs, and auto-incremented integer _serial_ numbers). However, `smax-postgres` 
can create the database and set up the required 'titles' table as needed (including indexing, and TimescaleDB 
extension as appropriate), when launched  with the `-b` (or `--bootstrap`) option.

```bash
  $ smax-postgres -c /usr/local/etc/smax-postgres/myconfig.cfg -b
```

Will log into the existing (new) database using the credentials specified in the configuration file 
`/usr/local/etc/smax-postgres/myconfig.cfg`, then configures that database (e.g. set up the TimescaleSB extension as 
appropriate), and creates the 'titles' table and its index.

You may also let the bootstrapping process create the database itself, in which case you may have to provide the
password for the 'postgres' admin account, or the credentials for another account with `CREATEDB` privileges with 
the necessary privileges to create databases. E.g.:

```bash
  $ smax-postgres -c /usr/local/etc/myconfig.cfg -b -p "S3cur1ty!"
```

will attempt create the database as the 'postgres' admin with password 'S3curity!', before proceeding to configure the 
newly created database as the user designated for the `smax-postgres` program. (Alternatively, you may use the `-a` and 
`-p` options together to create the database with another privileged user). The newly created database will be 
automatically assigned to the designated `smax-postgres` user as its owner).

Once the database is configured, you will not need the `-b` option again (but it also will not wreck the previous
initialization if accidentally used again after the initial setup).

----------------------------------------------------------------------------------------------------------------------


<a name="database-organization"></a>
## Database organization (for clients)

Each SMA-X variable has its own time-series data table in the SQL DB. These tables are named `var_<tid>`, where 
`<tid>` is a 6-digit serial number, e.g. `var_000001` for the first variable added to the SQL database. The variable 
name to `<tid>` pairings are listed in the `titles` table, which contains just two columns: (1) the full SMA-X 
variable id (_text_), and (2) the corresponding `<tid>` (integer _serial_) used in the SQL database to store the time 
series of the variable. (Thus, to figure out what table stored data for a given SMA-X variable you will need to search 
the 'titles' table for the `<tid>` first.)

The time series data in the `var_<tid>` tables contains (at least) 2 + `n` columns for an SMA-X variable, which has 
`n` array elements. The first column is the UTC timestamp at which the data was pulled from the SMA-X database. The 
second is an integer 'age' (in seconds) that informs how much before the pull was the last time that variable was 
updated in the SMA-X database. After that, the remaining columns list the array elements stored in the SMA-X variable. 
(Thus scalar entries will have just one additional column, labeled as 'c0').

Because the SMA-X variables may have dynamic types and array dimensions, the SQL tables may automatically expand 
to an enclosing type (for example if a variable changes from `int16` to `int32` or to a `float32`), and columns will 
be added as necessary to store an expanded set of array elements. When SMA-X data 'shrinks',  containing fewer 
elements than the existing SQL record, the SQL entry will be padded with `NULL` values as necessary.

In addition to the time-series data stored in the SQL database, it also stores versioned metadata in tables with
matching `<tid>` values. For example, the metadata for the time-series `var_000001` is stored in the table 
`var_000001_meta`. Metadata tables contained serial-numbered versions of infrequently changing metadata, such
as array dimensions and shapes (scalar values are stored with `ndim = 0` and `shape = NULL`), associated physical 
units, and downsampling factors. Each metadata entry is timestamped also to indicate when a change (if any) 
occurred in these characteristics.

Thus, to query an SMA-X variable `system:subsystem:property`, you first want to find the 'tid' of the variable in 
'titles':

```sql
  SELECT tid FROM titles WHERE name = 'system.subsystem.property';
```

Say the query returns the tid `192`, then the time-series for that variable will be stored in the table named
`var_000192`, while metadata versions are stored in the table named `var_000192_meta`.

----------------------------------------------------------------------------------------------------------------------

<a name="configuration-reference"></a>
## Configuration Reference

See `cfg/example.cfg` as an example configuration file. Based on it may create your own configuration file, which 
you can then load via the `-c` option to `smax-postgres` at startup. If using SystemD integration, you may want to update 
`/etc/systemd/system/smax-postgres.service` to load the configuration file from the location of your choice when the
service is started via `systemd`.

### Database configuration options

#### `smax_server <host>`

Host name or IP address of the SMA-X server (default 'smax').

#### `sql_auth <password>`

Password for authenticating user on the SQL server (no default).

#### `sql_db` <db-name>`

SQL database name to use (default is 'smax_db'). 

#### `sql_server <host>`

Host name or IP address of the SMA-X server (default 'localhost').

#### `sql_user <user-name>`

SQL user name to use (default is 'smax_db'). 

#### `use_hypertables <1|0>`

Determines whether to create hypertables via the TimescaleDB extension. The value 1 enables, 0 disables the used of 
hypertables (the default is to not use hypertables). TimescaleDB hypertables allow for faster access of time series 
data by organizing large datasets into smaller blocks of data, which can be handled more efficiently.

### Update frequency options

#### interval specification

Timescales may be specified by a numeric value (integer or decimal) followed immediately by a unit designator, such as 
'1d' for one day. Alternatively, the value 'none' can be used to disable a timescale-specific option. The following 
timescale units are understood:

 |  unit     | description |
 | --------- | ----------- |
 |   `s`     | second(s)   |
 |   `m`     | minute(s)   |
 |   `h`     | hour(s)     |
 |   `d`     | day(s)      |
 |   `w`     | week(s)     |
 |   `y`     | year(s)     |


#### `snapshot_interval <interval>`

Specifies the interval at which all designated variables are pushed into the SQL database, regardless whether they 
have changed or not since the last time they were pushed (default '1h'). See the section further above on interval 
specifications.

#### `update_interval <interval>`

Specifies the regular interval at which to push changing variables into the database (default: '1m'). See the section 
furtherabove on interval specifications. Variables that have no changed since the last update will be excluded from 
the regular updates until it is time for the next full snapshot. The snapshot interval is controlled separately (see 
above).


### Variable-specific options

#### glob patterns

SMA-X variables can be specified individually or via 
[glob patterns](https://man7.org/linux/man-pages/man7/glob.7.html), similarly to how these are used in UNIX shells. 


#### `always <pattern>`

Specifies a variable or a glob pattern of variables, which are to be logged into the SQL database regardless of all 
other directives, which may otherwise limit if and when they are to be pushed. Reserve using this option only for the 
most critical cases, when the other configuration options do not provide the desired level of assurances for some 
absolutely critical data points.

#### `exclude <pattern>`

Specifies a variable or a glob pattern of variables that are to be excluded from logging to the SQL database. 
`exclude` and `include` directives take effect in the order they were specified, so for a given variable only the last 
`include` or `exclude` statement, which pertains to it, will decide whether or not to log that given variable. 
Variables that are configured with an `always` directive will be logged to the SQL database regardless of any 
exclusions that may have been specified, either before or after. By default all metadata variables (ones whose name 
begin with `<`) and all temporary variables (whose names begin with an underscore `_`) are excluded from logging, 
unless they are explicitly re-included.

#### `include <pattern>`

Specifies a variable or a glob pattern of variables that are to be included for logging to the SQL database. 
`exclude` and `include` directives take effect in the order they were specified, so for a given variable only the last 
`include` or `exclude` statement, which pertains to it, will decide whether or not to log that given variable. 


#### `max_age <interval>`

Sets a maximum age for variables to push to the SQL database (default: '90d'). Variables that have not been updated in 
SMA-X for longer than the specified interval will not be pushed to the SQL database. Variables that are configured 
with an `always` directive will be logged to the SQL database regardless of their age.

#### `max_size <bytes>`

Sets a maximum byte size for variables to push to the SQL database (default: '1024'). Variables that have larger 
binary representations (after downsampling via the `sampling` directive, if any) will not be logged to the database to 
avoid bloating. However, variables explicitly configured via an `always` directive will be logged to the SQL database 
regardless of their storage requirements.

#### `sample <n> <pattern>`

Log sparse samples of data for a variable or a glob pattern of variables. In some cases you may store large arrays in 
the SMA-X database, logging of which may bloat the time series history stored in the SQL database. However, you may 
want to still get a preview of what that data was, by storing every n'th sample of the original only. For example, for 
an array with 1000 elemenrs, you may want to store say 20 samples. Setting `<n>` to 50 will achieve that, by storing 
every 50th element in the SQL database only. (Still, the SQL database will store the original dimensionality of the 
downsampled variables, and note the downsampling factor used also as metadata).

-----------------------------------------------------------------------------
Copyright (C) 2024 Attila Kovács


