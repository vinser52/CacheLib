break gdb_sync1
c
set scheduler-locking on
break acquire
c
c
c
c
thread 1
set scheduler-locking on
del 2
break gdb_sync2
c
thread 4
set scheduler-locking on
break gdb_sync3
c
thread 1
set scheduler-locking off
break gdb_sync4
c
c
