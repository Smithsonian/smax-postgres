---
excerpt: Record SMA-X history in PostgreSQL / TimescaleDB
---

<img src="/smax-postgres/resources/CfA-logo.png" alt="CfA logo" width="400" height="67" align="right"><br clear="all">

__smax-postgres__ is a daemon application, which can collect data from an SMA-X realtime database and insert these into 
a PostgreSQL database to create a time-series historical record for all or selected SMA-X variables. The program is
highly customizable and supports both regular updates for changing variables as well as regular snapshots of all 
selected SMA-X variables.

The __smax-postgres__ applications was created, and is maintained, by Attila Kovács at the Center for Astrophysics 
\| Harvard &amp; Smithsonian, and it is available through the 
[Smithsonian/redisx](https://github.com/Smithsonian/smax-postgres) repository on GitHub. 

This site contains various online resources that support the library:

__Downloads__

 - [Releases](https://github.com/Smithsonian/smax-postgres/releases) from GitHub

__Documentation__

 - [User's guide](doc/README.md) (`README.md`)
 - [API Documentation](apidoc/html/files.html)
 - [History of changes](doc/CHANGELOG.md) (`CHANGELOG.md`)
 - [Issues](https://github.com/Smithsonian/smax-postgres/issues) affecting __smax-postgres__ releases (past and/or present)
 - [Community Forum](https://github.com/Smithsonian/smax-postgres/discussions) &ndash; ask a question, provide feedback, or 
   check announcements.

__Dependencies__

 - [Smithsonian/smax-clib](https://github.com/Smithsonian/smax-clib) -- structured data exchange framework
 - [Smithsonian/redisx](https://github.com/Smithsonian/redisx) -- A C/C++ Redis client library
 - [Smithsonian/xchange](https://github.com/Smithsonian/xchange) -- structured data exchange framework
 - [Smithsonian/smax-server](https://github.com/Smithsonian/smax-server) -- SMA-X server configuration kit
 - PostgreSQL development files (`libpq.so` and `lipq-fe.h`)
 - __Popt__ (command-line options parsing) library development files (`libpopt.so` and `popt.h`)
 - (optional) __systemd__ (runtime management) development files (`libsystemd.so` and `sd-daemon.h`)
 
