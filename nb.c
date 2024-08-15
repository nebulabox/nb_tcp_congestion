#include <linux/module.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/math64.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
  #error NOT SUPPORT LINUX version < 5.8
#endif

struct nb_pkt
{
    u64 sec;
    u32 acked;
    u32 lost;
};

#define SLOTS_SIZE 10

struct nb
{
    struct nb_pkt slots[SLOTS_SIZE];
};

static void nb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct nb *nb = inet_csk_ca(sk);

    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    memset(nb->slots, 0, sizeof(nb->slots));
    cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
static void nb_cong_control(struct sock *sk, u32 ack, int flag, const struct rate_sample *rs)
#else
static void nb_cong_control(struct sock *sk, const struct rate_sample *rs)
#endif
{
    if (rs->delivered < 0 || rs->interval_us <= 0)
        return;

    struct tcp_sock *tp = tcp_sk(sk);
    struct nb *nb = inet_csk_ca(sk);
    u32 slot = 0;
    u64 sec = div_u64(tp->tcp_mstamp, USEC_PER_SEC); // 记录最后一次数据包到达的时
    div_u64_rem(sec, SLOTS_SIZE, &slot); // slot 存储余数

    if (nb->slots[slot].sec == sec)
    {
        nb->slots[slot].acked += rs->acked_sacked;
        nb->slots[slot].lost += rs->losses;
    }
    else
    {
        nb->slots[slot].sec = sec;
        nb->slots[slot].acked = rs->acked_sacked;
        nb->slots[slot].lost = rs->losses;
    }

    u64 min_sec = sec - SLOTS_SIZE;
    u32 acked = 0, lost = 0;
    u64 rate = 13107200; // 100mbps
    u32 cwnd;

    u32 mss = tp->mss_cache; // MSS 是 TCP 连接中可以发送的最大数据包大小
    u32 rtt_ms = (tp->srtt_us >> 3) / USEC_PER_MSEC; // SRTT 是 TCP 用于估计网络延迟的指标
    if (!rtt_ms) rtt_ms = 1;

    for (int i = 0; i < SLOTS_SIZE; i++)
    {
        if (nb->slots[i].sec >= min_sec)
        {
            acked += nb->slots[i].acked;
            lost += nb->slots[i].lost;
        }
    }

    u32 ack_rate = acked * 100 / (acked + lost);
    if (ack_rate < 10) ack_rate = 10;

    rate *= 100;
    rate = div_u64(rate, ack_rate);

    cwnd = div_u64(rate, MSEC_PER_SEC);
    cwnd = ((cwnd * rtt_ms) / mss) * 2; 
    cwnd = max_t(u32, cwnd, 19);

    tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);

    WRITE_ONCE(sk->sk_pacing_rate, min_t(u64, rate, READ_ONCE(sk->sk_max_pacing_rate)));
}

static u32 nb_undo_cwnd(struct sock *sk)
{
    return tcp_sk(sk)->snd_cwnd;
}

static u32 nb_ssthresh(struct sock *sk)
{
    return tcp_sk(sk)->snd_ssthresh;
}

static struct tcp_congestion_ops tcp_nb_ops = {
    .flags = TCP_CONG_NON_RESTRICTED,
    .name = "nb",
    .owner = THIS_MODULE,
    .init = nb_init,
    .cong_control = nb_cong_control,
    .undo_cwnd = nb_undo_cwnd,
    .ssthresh = nb_ssthresh,
};

static int __init nb_register(void)
{
    return tcp_register_congestion_control(&tcp_nb_ops);
}

static void __exit nb_unregister(void)
{
    tcp_unregister_congestion_control(&tcp_nb_ops);
}

module_init(nb_register);
module_exit(nb_unregister);

MODULE_AUTHOR("Nebula Box");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NB TCP Congestion");
MODULE_VERSION("24.08.15");
