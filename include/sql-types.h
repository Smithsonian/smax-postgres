/**
 * @file
 *
 * @date Created  on Jan 15, 2024
 * @author Attila Kovacs
 *
 *   Equivalent SQL data types across many common SQL flavors
 *
 */

#ifndef SQL_TYPES_H_
#define SQL_TYPES_H_


#define SQL_MIN_DOUBLE          (1e-100)   ///< SQL does not like doubles with exponent above +- 307
#define SQL_MAX_DOUBLE          (1e100)    ///< Alternatively use DBL_MIN / DBL_MAX from <float.h>...

// Logical values
#define SQL_BOOLEAN             "BOOLEAN"  ///< SQL type for a logical true/false value

// Numerical types
#if MS_ACCESS
#  define SQL_NUMBER            "NUMBER"      ///< MS Access type for a generic numerical value
#  define SQL_INT8              SQL_NUMBER    ///< MS Access type for 8-bit integer
#  define SQL_INT16             SQL_NUMBER    ///< MS Access type for 16-bit integer
#  define SQL_INT32             SQL_NUMBER    ///< MS Access type for 32-bit integer
#  define SQL_INT64             SQL_NUMBER    ///< MS Access type for 64-bit integer
#  define SQL_FLOAT             SQL_NUMBER    ///< MS Access type for single precision (32-bit) floating point value
#  define SQL_DOUBLE            SQL_NUMBER    ///< MS Access type for double precision (64-bit) floating point value
#else
#  if MYSQL | SQL_SERVER
#     define SQL_INT8              "TINYINT"     ///< MySQL / SQL-Server: 8-bit integer
#  else
#     define SQL_INT8              "SMALLINT"    ///< 8-bit integer is not standard SQL type so bump to 16-bits
#  endif
#  define SQL_INT16             "SMALLINT"    ///< SQL type for 16-bit integer
#  define SQL_INT32             "INTEGER"     ///< SQL type for 32-bit integer
#  define SQL_INT64             "BIGINT"      ///< SQL type for 64-bit integer
#  if SQL_SERVER
#    define SQL_FLOAT             "REAL"        ///< SQL-Server type for single precision (32-bit) floating point value
#    define SQL_DOUBLE            "FLOAT"       ///< SQL-Server type for double precision (64-bit) floating point value
#  elif MYSQL || ORACLE
#    define SQL_FLOAT             "FLOAT"       ///< MySQL / Oracle type for single precision (32-bit) floating point value
#    define SQL_DOUBLE            "DOUBLE"      ///< MySQL / Oracle type for double precision (64-bit) floating point value
#  else
#    define SQL_FLOAT             "REAL"                ///< SQL type for single precision (32-bit) floating point value
#    define SQL_DOUBLE            "DOUBLE PRECISION"    ///< SQL type for single precision (32-bit) floating point value
#  endif
#endif

// Timestamps
#if POSTGRES
#  define SQL_DATE              "TIMESTAMPTZ"         ///< Timescale DB requires TIMESTAMPTZ (not TIMESTAMP)!
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S UTC'"   ///< PostgreSQL date format
#elif MS_ACCESS
#  define SQL_DATE              "DATE"                ///< MS Access date type
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S'"       ///< MS Access date format
#else
#  define SQL_DATE              "TIMESTAMP"           ///< SQL date type
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S'"       ///< SQL date format
#endif


// Generic variable-length ASCII text types
#if POSTGRES || MYSQL
#  define SQL_TEXT              "TEXT"                ///< Postgres / MySQL text type.
#elif SQL_SERVER
#  define SQL_TEXT              "VARCHAR(MAX)"        ///< SQL server text type
#elif MS_ACCESS
#  define SQL_TEXT              "LONG TEXT"           ///< MS Access text type
#elif ORACLE
#  define SQL_TEXT              "LONG"                ///< Oracle text type
#else
#  define SQL_TEXT              "VARCHAR"             ///< SQL text type
#endif


// Indexable types for storing SMA-X variable names
#if SQL_SERVER
#  define SQL_VARNAME           "VARCHAR(8000)"       ///< SQL Server cannot index on VARCHAR(MAX), so use a more limited type
#else
#  define SQL_VARNAME           SQL_TEXT              ///< SQL type for storing variable names.
#endif

// Automatically incremented serial numbers
#if POSTGRES
# define SQL_SERIAL             "SERIAL"              ///< PostgreSQL type for serial numbers
#elif SQL_SERVER
# define SQL_SERIAL             "IDENTITY"            ///< SQL Server type for serial numbers
#elif MYSQL || ORACLE
# define SQL_SERIAL             SQL_INT32 " AUTO_INCREMENT"   ///< MySQL / Oracle type for serial numbers
#elif MS_ACCESS
# define SQL_SERIAL             "COUNTER"             ///< MS Access type for serial numbers
#else
# define SQL_SERIAL             SQL_INT32 " NOT NULL UNIQUE"    ///< SQL type for serial numbers
#endif


// Snipplet for selecting last entry
#define SQL_LAST(id)            "ORDER BY " id " DESC LIMIT 1"    ///< SQL for selecting last entry




#endif /* SQL_TYPES_H_ */
