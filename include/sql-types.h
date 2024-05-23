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
#define SQL_BOOLEAN             "BOOLEAN"

// Numerical types
#if MS_ACCESS
#  define SQL_NUMBER            "NUMBER"
#  define SQL_INT8              SQL_NUMBER
#  define SQL_INT16             SQL_NUMBER
#  define SQL_INT32             SQL_NUMBER
#  define SQL_INT64             SQL_NUMBER
#  define SQL_FLOAT             SQL_NUMBER
#  define SQL_DOUBLE            SQL_NUMBER
#else
#  if MYSQL | SQL_SERVER
#     define SQL_INT8              "TINYINT"
#  else
#     define SQL_INT8              "SMALLINT"      ///< 8-bit integer is not standard SQL type so bump to 16-bits
#  endif
#  define SQL_INT16             "SMALLINT"
#  define SQL_INT32             "INTEGER"
#  define SQL_INT64             "BIGINT"
#  if SQL_SERVER
#    define SQL_FLOAT             "REAL"
#    define SQL_DOUBLE            "FLOAT"
#  elif MYSQL || ORACLE
#    define SQL_FLOAT             "FLOAT"
#    define SQL_DOUBLE            "DOUBLE"
#  else
#    define SQL_FLOAT             "REAL"
#    define SQL_DOUBLE            "DOUBLE PRECISION"
#  endif
#endif

// Timestamps
#if POSTGRES
#  define SQL_DATE              "TIMESTAMPTZ"      ///< Timescale DB requires TIMESTAMPTZ (not TIMESTAMP)!
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S UTC'"
#elif MS_ACCESS
#  define SQL_DATE              "DATE"
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S'"
#else
#  define SQL_DATE              "TIMESTAMP"
#  define SQL_DATE_FORMAT       "'%F %H:%M:%S'"
#endif


// Generic variable-length ASCII text types
#if POSTGRES || MYSQL
#  define SQL_TEXT              "TEXT"
#elif SQL_SERVER
#  define SQL_TEXT              "VARCHAR(MAX)"
#elif MS_ACCESS
#  define SQL_TEXT              "LONG TEXT"
#elif ORACLE
#  define SQL_TEXT              "LONG"
#else
#  define SQL_TEXT              "VARCHAR"
#endif


// Indexable types for storing SMA-X variable names
#if SQL_SERVER
#  define SQL_VARNAME           "VARCHAR(8000)"      // SQL Server cannot index on VARCHAR(MAX), so use a more limited type
#else
#  define SQL_VARNAME           SQL_TEXT
#endif

// Automatically incremented serial numbers
#if POSTGRES
# define SQL_SERIAL             "SERIAL"
#elif SQL_SERVER
# define SQL_SERIAL             "IDENTITY"
#elif MYSQL || ORACLE
# define SQL_SERIAL             SQL_INT32 " AUTO_INCREMENT"
#elif MS_ACCESS
# define SQL_SERIAL             "COUNTER"
#else
# define SQL_SERIAL             SQL_INT32 " NOT NULL UNIQUE"
#endif


// Snipplet for selecting last entry
#define SQL_LAST(id)            "ORDER BY " id " DESC LIMIT 1"




#endif /* SQL_TYPES_H_ */
