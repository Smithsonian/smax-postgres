# smaxLogger: Logging SMA-X variables into a PostgreSQL time-series database.

`smaxLogger` is a daemon program, which can collect data from an SMA-X realtime database and insert these into a
PostgreSQL database to create a time-series historical record for all or selected SMA-X variables. The program is
highly customizable and supports both regular updates for changing variables as well as regular snapshots of all 
selected SMA-X variables.

## Table of Contents

- [Installation](#installation)
- [Database organization (for clients)](#database-organization)
- [Configuration reference](#configuration-reference)

----------------------------------------------------------------------------------------------------------------------

<a name="installation"></a>
## Installation

To build `smaxLogger` from source, you'll need to install a couple of build dependencies. First, make sure you 
have the standard C development tools (such as `gcc` with `glibc` and corresponding headers, GNU `make`). You'll 
also need a PostgreSQL installation, complete with headers and development libraries (`-dev` package(s) in 
Debian-based distros or `-devel` package(s) in RPM based distros), as well as the `popt` development libraries 
(`libpopt-dev`in Debian, or `popt-devel` in RPM distros) for command line option processing.

### Building `smaxLogger` from source

Before you compile `smaxLogger`, check and edit the `Makefile` as necessary.

You can change the default install location(s) by editing the `INSTALL_*` variables to customize to your needs. 
You can also enable or disable systemd integration by setting or commenting the `SYSTEMD=1` line (qhich requires that
you install the systemd development packages also, such as `libsystemd-dev` on Debian or `systemd-devel` on RPM-based
distros).

To use the TimescaleDB extension, you will need to install the extension package also. And, if using an older version 
of TimescaleDB (prior to 2.13), you may want to uncomment the `DFLAGS += -DTIMESCALEDB_OLD=1` line in the `Makefile`.

Next you need to `source setup.sh` from the root directory of the repository. This configures variables for the build 
(sort of like `./configure` would for more typical POSIX builds).

Now you may compile `smaxLogger`:

```bash
  $ make
```

Provided the build was successful, you can install the executables, configuration files, and optionally the systemd 
unit files via:

```bash
  $ sudo make install
```

(In case of systemd integration it will also reload the daemon so `smaxLogger.service` can be enabled and managed as 
desired.)


### Standard error/output with systemd integration

In case of systemd integration, errors will get logged to the journal, and can be investigated by `journalctl`. E.g. to
see the errors in the last 3 hours, you may:

```bash
  $ journalctl -u smaxLogger --since "3 hours ago"
```

Standard output is logged to the file `/var/log/smaxLogger.out` for the current session. Restarting `smaxLogger` will 
start a new file. (Because of buffering, there may be a long lag before you'll see stuff appear in this log file.)



### Initial setup of the SQL database

Prior to using `smaxLogger`, you will need to configure users and access privileges (roles) for the database, and may 
want to create the database instance manually. You will need to create a user designated for the `smaxLogger` program, 
and specify its credentials in the `smaxLogger` configuration file. This user will not require `CREATEDB` permission, 
but it will need permission to create tables in the existing database and to insert data or to search the tables
(i.e. read/write privileges). Additionally, you may also create the designated database instance assigned to whatever 
user to own. If you create the database manually, do not forget to set the name of the designated database in the `smaxLogger` configuration file.

Normally `smaxLogger` will assume that the database to use has been fully set up, including a 'titles' table 
(containing 2 columns: _text_ variable IDs, and auto-incremented integer _serial_ numbers). However, `smaxLogger` 
can create the database and set up the required 'titles' table as needed (including indexing, and TimescaleDB 
extension as appropriate), when launched  with the `-b` (or `--bootstrap`) option.

```bash
  $ smaxLogger -c /usr/local/etc/smaxLogger/myconfig.cfg -b
```

Will log into the existing (new) database using the credentials specified in the configuration file 
`/usr/local/etc/smaxLogger/myconfig.cfg`, then configures that database (e.g. set up the TimescaleSB extension as 
appropriate), and creates the 'titles' table and its index.

You may also let the bootrapping process create the database itself, in which case you may have to provide the
password for the 'postgres' admin account, or the credentials for another account with `CREATEDB` privileges with 
the necessary privileges to create databases. E.g.:

```bash
  $ smaxLogger -c /usr/local/etc/myconfig.cfg -b -p "S3cur1ty!"
```

will attempt create the database as the 'postgres' admin, whose password is 'S3cur1ty!', before proceeding to 
configure the newly created database as the user designated for the `smaxLogger` program. (Alternatively, you may 
use the `-a` and `-p` options together to create the database with another privileged user). The newly created 
database will be automatically assigned to the designated `smaxLogger` user as its owner).

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
you can then load via the `-c` option to `smaxLogger` at startup. If using systemd integration, you may want to update 
`/etc/systemd/system/smaxLogger.service` to load the configuration file from the location of your choice when the
service is started via systemd.

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
begin with `<`) and all temporary variable (whose names begin with an underscore `_`) are excluded from logging, 
unless they are explicitly re-included.

#### `include <pattern>`

Specifies a variable or a glob pattern of variables that are to be excluded from logging to the SQL database. 
`exclude` and `include` directives take effect in the order they were specified, so for a given variable only the last 
`include` or `exclude` statement, which pertains to it, will decide whether or not to log that given variable. 
Variables that are configured with an `always` directive will be logged to the SQL database regardless of any 
exclusions that may have been specified, either before or after. By default, all SMA-X variables are included in the 
logging, except for the metadata and temporary data that are excluded by default (see `exclude`).


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
an array with 1000 entries, you may want to store say 20 samples. Setting `<n>` to 50 will achieve that, by storing 
every 50th element in the SQL database only. (Still, the SQL database will store the original dimensionality of the 
downsampled variables, and note the downsampling factor used also as metadata).


