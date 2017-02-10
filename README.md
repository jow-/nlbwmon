# nlbwmon - Simple conntrack netlink based traffic accounting

## Description

nlbwmon is a bandwidth monitoring daemon which uses a netlink socket to pull usage information from the linux kernel.  

Use this repository as a package feed:

    cp feeds.conf.default feeds.conf
    echo "src-git nlbwmon https://github.com/jow-/nlbwmon.git" >> feeds.conf
    ./scripts/feeds update nlbwmon
    ./scripts/feeds install nlbwmon

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
