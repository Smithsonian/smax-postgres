# smaxLogger configuration file for the Submillimeter Array (SMA)
#
# Author: Attila Kovacs
# Date: 2024 January 19
# ----------------------------------------------------------------------------

# Set the host name or IP address of the SMA-X server (default: 'smax').
#smax_server smax

# Set the host name or IP address of the SQL server (default: 'localhost').
#sql_server localhost

# Set the SQL database name (default: 'smax_db').
sql_db engdb

# Set the SQL user name (default: 'smax_db').
sql_user loggerserver

# Set the SQL password (default: none).
sql_auth smaEngdb

# Whether to use Timescale DB hyper tables. They allow for faster access to
# time-ordered data, but not all SQL databases support it (default: 'false').
# The setting affects the creation of new SQL tables only. Tables that have
# been created before will not be altered.
use_hyper_tables true

# Set the interval between for regular logging of recently updated variables 
# (default: 1m). See how timescales are specified at the top. 
update_interval 1m

# Set the interval between snapshots of all variables, regardless of whether
# they have been updated recently (default: 1h). See how timescales are 
# specified at the top.
snapshot_interval 1h

# Set the maximum byte size of variables to be logged (default: 1024). 
# Variables that would log larger data will be ignored unless they are 
# explicitly force to via an 'always' directive. For example, a variable
# that stores an array of a 100 double-precision values will require 100 * 8
# bytes of space, that is 800 bytes of storage.
max_size 1024

# Set the maximum age of variables to be logged (default: '90d'). Variables
# that have not been updating for longer than the specified timescale will
# not be logged, unless they are forced by an 'always' directive.
max_age	90d

# Variables and patterns to be excluded from logging. However, variables that 
# are excluded, may be forced to log still via an 'always' directive. By 
# default the patterns '_*' or '*:_*' (designated temporary and test 
# tables/variables) and '*<' or '*:<*' (designated metadata tables/variables) 
# will be exluded automatically. 
#exclude _*
#exclude *:_*
#exclude <*
#exclude *:<*

# exclude variables logged separately in the legacy engdb
exclude RM:*
exclude DSM:*

# Variables and patterns to be (re)included in the logging, which may have been
# excluded by prior 'exclude' statements.
#include something:important:*

# You can force variables to be logged, no matter the other blocking settings
# (exclusions, size or age limits) that may apply to it, via an 'always'
# directive. It is effectively a stronger version of 'include', which overrides
# any other settings.
#always essential:*

# Set a sparse sampling for large data, so that instead of storing a large
# array of values, only every n^th value is stored in the SQL database. When
# a samping is set, the 'max_size' limit applies to the volume of the 
# selected sparse samples, rather than to the volume of the original data.
# The first argument in the downsampling factor (usually >1), followed by
# the variable name or pattern to which the sampling applies.

# SWARM cgains 8 * 2 * 16384 values
#sample 128 *:input:?:cgains

# Scanning spectrometer derived Tsys (560 floats)
sample 10  *:scanspec:tsys

# Scanning spectrometer raw ADC values (560 floats)
sample 10  *:scanspec:channel_adc

# Scanning spectrometer raw ADC values on cal load (560 floats)
sample 10  *:scanspec:cal_channel_adc

  


