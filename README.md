Welcome to the kernel infra to update netdev stats in linux kernel wiki!

In NFV SDN use-cases,there may a requirement to update network interface stats in linux kernel.All packet processing functionality happens at datapath and and kernel interface stats does not really reflect the packets forwarded/processed by datapath.

Packets statistics can be retrieved from opeflow aware switch using openflow message and using this kernel module ,packet statistics can be updated.

sysfs interface (kobject creation and sysfs group creation ) is provided to userspace to enable netdev stats update on a give interface.

  <pre>
echo "pr-s1-eth1" >  /sys/kernel/update_stats/update_stats 
</pre>
On sys file store function invocation (which will be called when the file is written in sysfs) , netstat group is added for the kernel object and corresponding store/ show functions are registered .

<pre>
ls -lhrt /sys/kernel/pr-s1-eth1/statistics/
total 0
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_window_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_packets
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_heartbeat_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_fifo_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_dropped
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_compressed
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_carrier_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_bytes
-rw-r--r-- 1 root root 4.0K 10 24 17:32 tx_aborted_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_packets
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_over_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_missed_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_length_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_frame_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_fifo_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_dropped
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_crc_errors
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_compressed
-rw-r--r-- 1 root root 4.0K 10 24 17:32 rx_bytes
-rw-r--r-- 1 root root 4.0K 10 24 17:32 multicast
-rw-r--r-- 1 root root 4.0K 10 24 17:32 collisions
</pre>

To update stats on a particular netdev device ,respective kernel sys file in interface statistics directory should be written to. Updated stat value can be seen in ifconfig output.
<pre>
echo 12345 >  /sys/kernel/pr-s1-eth2/statistics/rx_bytes
</pre>
