# nlbwmon - Simple conntrack netlink based traffic accounting

## Description

nlbwmon can be used on a linux router to monitor bandwidth used by network hosts.  Network statistics are collected and stored in a database.  The client utility (nlbw) can query the daemon for current statistics.  

nlbwmon uses a netlink socket to pull usage information from the linux kernel.  nlbwmon collects statistic information from linux conntrack entries.  This method is quite efficient compared to other methods of monitoring bandwidth usage.

Each time the conntrack entries are polled, their counters are reset (zero-on-read).  When a conntrack entry is destroyed, nlbwmon is notified by the kernel, and stats are collected from that entry before it is deleted.

## Usage

### nlbwmon

*NOTE: an init script and config file is provided for lede, allowing these settings to be configured via uci.  Just take a look at /etc/config/nlbwmon.*

<dl>
<dt>-i [sec]</dt>
<dd>Interval used to save in-memory database to file.</dd>

<dt>-r [sec]</dt>
<dd>Interval used to poll the conntrack entries.</dd>

<dt>-s [network]</dt>
<dd>Specify network subnet to monitor.</dd>

<dt>-o [path-to-database-file]</dt>
<dd>Storage directory for the database files.</dd>

<dt>-p [path-to-procol-database]</dt>
<dd>Protocol description file, used to distinguish traffic streams by IP protocol number and port.</dd>

<dt>-G [count]</dt>
<dd>Number of database generations to retain.  After the limit is reached, the oldest database files are deleted.</dd>

<dt>-I [interval]</dt>
<dd>Accounting period interval.  May be either in the format YYYY-MM-DD/NN
to start a new accounting period exactly every NN days, beginning at
the given date, or a number specifiying the day of month at which to
start the next accounting period.  For example:</dd>
</dl>

```
2017-01-17/14   # every 14 days, starting Jan 17, 2017
-2              # second last day of month, e.g. 30th in March
1               # first day of the month (default)
```

<dl>
<dt>-P</dt>
<dd>Whether to preallocate the maximum possible database size in memory.
This is mainly useful for memory constrained systems which might not
be able to satisfy memory allocation after longer uptime periods.
Only effective in conjunction with database_limit, ignored otherwise.</dd>

<dt>-Z</dt>
<dd>Whether to gzip compress archive databases. Compressing the database
files makes accessing old data slightly slower but helps to reduce
storage requirements.</dd>
</dl>


### nlbw
*NOTE: See the examples below to get started quickly.*

<dl>
<dt>-S</dt>
<dd></dd>

<dt>-c</dt>
<dd>Specify a command.  Current commands are: show, json, csv, list, commit</dd>

<dt>-p [path-to-procol-database]</dt>
<dd>Protocol description file, used to distinguish traffic streams by IP protocol number and port.</dd>

<dt>-g</dt>
<dd>Group output by the specified column.  Prefix with a - to invert order.</dd>

<dt>-o</dt>
<dd>Order output by the specified column.  Prefix with a - to invert order.</dd>

<dt>-t</dt>
<dd>When listing records, specify a day to retreive, in the format YYYY-MM-DD</dd>

<dt>-n</dt>
<dd>Use plain numbers, dont divide to get K, M, G, etc.</dd>

<dt>-s [char]</dt>
<dd>Specify the column separator when using CSV format.</dd>

<dt>-q [char]</dt>
<dd>Specify the quote character when using CSV format.</dd>

<dt>-e [char]</dt>
<dd>Specify the escape character when using CSV format.</dd>
</dl>

### Commands available for nlbw:

#### show

#### json

#### csv

#### list

#### commit

## Use this repository as a package feed:

You can easily build nlbwmon from lede by including this repository in your build environment:

    cp feeds.conf.default feeds.conf
    echo "src-git nlbwmon https://github.com/jow-/nlbwmon.git" >> feeds.conf
    ./scripts/feeds update nlbwmon
    ./scripts/feeds install nlbwmon

## Examples

Once the package is installed, use the "nlbw" command to dump gathered values:

    root@jj:~# nlbw -c show
      Fam            Host (    MAC )      Layer7      Conn.   > Downld. ( > Pkts. )      Upload (   Pkts. )
    IPv4       10.11.12.7 (47:e1:b9)       HTTPS       111      2.99 MB (   4.38 K)   816.77 KB (   5.86 K)
    IPv6  2001:470:527e::6666:b3ff:fe47:e1b9 (47:e1:b9)       HTTPS        14    546.07 KB (     930 )   196.05 KB (     932 )
    IPv6  2001:470:527e::6666:b3ff:fe47:e1b9 (47:e1:b9)        HTTP        15    296.07 KB (     194 )    28.67 KB (     329 )
    IPv4     10.11.12.167 (a3:5c:1c)       HTTPS        24    210.53 KB (     299 )    61.86 KB (     400 )
    IPv4      10.11.12.17 (2c:97:19)       HTTPS        31    163.54 KB (     481 )   352.28 KB (     593 )
    IPv4       10.11.12.7 (47:e1:b9)       other       353    104.52 KB (     610 )    64.79 KB (     709 )
    IPv4      10.11.12.15 (94:59:06)       HTTPS         6     49.48 KB (      85 )    18.36 KB (     103 )
    IPv4       10.11.12.7 (47:e1:b9)        HTTP        24     14.24 KB (     152 )    16.89 KB (     177 )
    IPv4     10.11.12.167 (a3:5c:1c)       other        15      6.03 KB (      80 )     5.73 KB (      68 )
    IPv4      10.11.12.17 (2c:97:19)       other        11        800 B (      16 )     3.11 KB (      47 )
    IPv6  2001:470:527e::6666:b3ff:fe47:e1b9 (47:e1:b9)   IPv6-ICMP         1        728 B (       7 )     5.68 KB (      56 )
    IPv4      10.11.12.17 (2c:97:19)        HTTP         1        456 B (       5 )       450 B (       6 )
    IPv4       10.11.12.7 (47:e1:b9)        ICMP         1        420 B (       5 )       420 B (       5 )
    IPv4      10.11.12.15 (94:59:06)       other         3        228 B (       3 )       228 B (       3 )
    IPv6  2001:470:527e::a886:c6fc:82a4:bba9 (2c:97:19)        HTTP         1         80 B (       1 )       144 B (       2 )
    IPv6  2001:470:527e::a886:c6fc:82a4:bba9 (00:00:00)        HTTP         2          0 B (       0 )         0 B (       0 )
    IPv6  2001:470:527e::1 (35:88:49)        HTTP        18          0 B (       0 )     1.64 KB (      21 )

Use -g (group) and -o (order) flags to influence output:

    root@jj:~# nlbw -c show -g mac,fam -o conn
      Fam               MAC    < Conn.     Downld. (   Pkts. )      Upload (   Pkts. )
    IPv6  74:81:14:2c:97:19         1         80 B (       1 )       144 B (       2 )
    IPv6  00:00:00:00:00:00         2          0 B (       0 )         0 B (       0 )
    IPv4  a0:99:9b:94:59:06         9     49.70 KB (      88 )    18.59 KB (     106 )
    IPv6  00:0d:b9:35:88:49        19          0 B (       0 )     1.64 KB (      21 )
    IPv6  64:66:b3:47:e1:b9        32    857.92 KB (   1.16 K)   242.99 KB (   1.34 K)
    IPv4  00:f7:6f:a3:5c:1c        39    216.57 KB (     379 )    67.59 KB (     468 )
    IPv4  74:81:14:2c:97:19        43    164.77 KB (     502 )   355.83 KB (     646 )
    IPv4  64:66:b3:47:e1:b9       501      3.12 MB (   5.19 K)   905.20 KB (   6.79 K)

Prefix order field names with dash to invert order:

    root@jj:~# nlbw -c show -g mac -o -conn,-tx
                  MAC    > Conn.     Downld. (   Pkts. )    > Upload (   Pkts. )
    64:66:b3:47:e1:b9       542      3.96 MB (   6.38 K)     1.12 MB (   8.16 K)
    74:81:14:2c:97:19        44    164.85 KB (     503 )   355.97 KB (     648 )
    00:f7:6f:a3:5c:1c        42    216.61 KB (     380 )    67.63 KB (     469 )
    00:0d:b9:35:88:49        20          0 B (       0 )     1.79 KB (      23 )
    a0:99:9b:94:59:06         9     49.70 KB (      88 )    18.59 KB (     106 )
    00:00:00:00:00:00         2          0 B (       0 )         0 B (       0 )

Use the json query to dump the raw database values in JSON format:

    root@jj:~# nlbw -c json
    ...
