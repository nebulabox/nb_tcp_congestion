# nb_tcp_congestion
A TCP congestion kernel module that sacrifices everything for the highest speed.

edit /etc/sysctl.conf
    net.core.default_qdisc=fq
    net.ipv4.tcp_congestion_control=bbr

sysctl -p

sysctl net.ipv4.tcp_available_congestion_control

sysctl net.ipv4.tcp_congestion_control

