#include <net/genetlink.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/timer.h>
#include <linux/netdevice.h>


/* Command that will be sent by application */
#define NOTIFY_ON   0x11
#define NOTIFY_OFF  0x12

/* The number of packets to wait before informing the application */
#define NOTIFY_ON_PKT_COUNT  4

/* Notification data that will be sent on the reply */
#define NOTIFY_THRESHOLD_REACHED 0x20
int notify_threshold = NOTIFY_ON_PKT_COUNT ;
int notify_timelimit = 1 ;

/* For now it is just the command.  */
typedef struct _tph_info {
	int command ;
} tph_info ;



enum {
	PKT_MON_A_UNSPEC,
	PKT_MON_CMD,
    __PKT_MON_A_MAX,
};

#define PKT_MON_A_MAX (__PKT_MON_A_MAX - 1)

static struct nla_policy pkt_mon_genl_policy[PKT_MON_A_MAX + 1] = {
	[PKT_MON_CMD] = { .type = NLA_NUL_STRING },
};

#define VERSION_NR 1

static struct genl_family pkt_mon_gnl_family = {
	.id      = GENL_ID_GENERATE,         
	.hdrsize = 0,
	.name 	 = "PKT_MON",        	
	.version = VERSION_NR,              
	.maxattr = PKT_MON_A_MAX,
};

int pkt_mon_doit(struct sk_buff *, struct genl_info *);

struct genl_ops pkt_mon_gnl_ops_doit = {
	.cmd = PKT_MON_CMD,
	.flags = 0,
	.policy = pkt_mon_genl_policy,
	.doit = pkt_mon_doit,
	.dumpit = NULL,
};

static int 	    saved_pid ;
static struct 	nf_hook_ops nfho;        
volatile int 	monitor_enabled = 0 ;
unsigned long 	monitor_timeout = 0 ; 

void
send_mesg_to_user_app(void)
{
    struct sk_buff *skb;
    int rc;
    void *msg_head;

    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
    if (skb == NULL) {
        return ;
    }

    msg_head = genlmsg_put(skb, 0, 0 , &pkt_mon_gnl_family, 0, PKT_MON_CMD);
    if (msg_head == NULL) {
        return ;
    }

    rc = nla_put_u32(skb, PKT_MON_CMD, NOTIFY_THRESHOLD_REACHED );
    if (rc != 0) {
        return ;
    }
	
    genlmsg_end(skb, msg_head);
    genlmsg_unicast(&init_net, skb,saved_pid );
	
	return ;
}


/*
 * Netfilter callback which will send the message back to userspace.
 * When monitoring is not enabled, it will fail the first check and will return.
 * Once monitoring is enabled, counter will get incremented for every packet
 * going out on the wan interface, and once the threshold reaches, or if
 * the time exceeds, then message will be sent to userspace and monitoring
 * will be disabled.
 */

unsigned int 
hook_func(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, 
			const struct net_device *out, int (*okfn)(struct sk_buff *))
{
    static int counter = 0 ;


    /* monitor_enabled flag, gets enabled once in 10 minutes are greater. */     
    if (unlikely(monitor_enabled)) { 

        /*  Dont want to interrupt immediately, wait for few packets to go, or atleast
            notify_timelimit second to pass 
        */
        if (counter++ > notify_threshold || time_after(jiffies, monitor_timeout)) {
            send_mesg_to_user_app();
            counter = 0 ;
            monitor_enabled = 0 ;
        }
    } else  {
        counter = 0 ;
    }

    return NF_ACCEPT;                                                        
}

/*
 * Callback that will act on the command.
 * Will create an reply SKB and enable packet monitoring
 * on the netfilter hook. 
 */


int pkt_mon_doit(struct sk_buff *skb_2, struct genl_info *info)
{
    struct nlattr *na;
    tph_info *tinfo;
	
    pr_debug("pkt_mon: Received a TPH COMMAND :\n");

    if (info == NULL ){
        pr_err("pkt_mon: NULL info\n");
        goto out ;
    }
  
    na = info->attrs[PKT_MON_CMD];
    if (!na) {
        pr_err("pkt_mon: no info->attrs %i\n", PKT_MON_CMD);
        goto out ;
    }

    tinfo = (tph_info *)nla_data(na);
    if (tinfo == NULL) {
        pr_err("pkt_mon: tinfo is null\n");
        goto out ;
    }

    pr_debug("received: Command %d\n", tinfo->command);

    switch (tinfo->command) {
        case (NOTIFY_ON): 
            if (!monitor_enabled) {
                saved_pid  = info->snd_pid ;
                monitor_timeout = jiffies + ( notify_timelimit * HZ );
                wmb();
                monitor_enabled = 1 ;
            }
            break ;
        case (NOTIFY_OFF):
            monitor_enabled = 0 ;
            break ;
        default :
            pr_err("pkt_mon : Unknown command %i\n", PKT_MON_CMD);
            goto out ;
    }

    return 0;

out:
    pr_err("pkt_mon : error in pkt_mon_doit:\n");
    return -1 ;
}

static int __init pkt_mon_init(void)
{
    int rc;
        
    /*register new family*/
    rc = genl_register_family(&pkt_mon_gnl_family);
    if (rc != 0)
        goto failure;

    /*register functions (commands) of the new family*/
    rc = genl_register_ops(&pkt_mon_gnl_family, &pkt_mon_gnl_ops_doit);
    if (rc != 0){
        pr_err("register oops: %i\n",rc);
        genl_unregister_family(&pkt_mon_gnl_family);
        goto failure;
    }

    nfho.hooknum = NF_INET_POST_ROUTING;         
    nfho.hook = hook_func;                      
    nfho.pf = PF_INET;                           //Only Interested in IPV4 packets
    nfho.priority = NF_IP_PRI_LAST ;           
    nf_register_hook(&nfho);                  

    pr_debug("On with threshold =%d, timelimit = %d\n", notify_threshold, notify_timelimit);

    return 0;
	
failure:
    pr_err("an error occured while inserting the generic netlink example module\n");
    return -1;
}

static void __exit pkt_mon_exit(void)
{
    int ret;

    pr_debug("Exit\n");

    nf_unregister_hook(&nfho);                     //cleanup – unregister hook

    ret = genl_unregister_ops(&pkt_mon_gnl_family, &pkt_mon_gnl_ops_doit);
    if(ret != 0){
        pr_err("unregister ops: %i\n",ret);
        return;
    }

    /*unregister the family*/
    ret = genl_unregister_family(&pkt_mon_gnl_family);
    if(ret !=0){
        pr_err("unregister family %i\n",ret);
    }
}


module_param(notify_timelimit, int, 0644);
module_param(notify_threshold, int, 0644);
module_init(pkt_mon_init);
module_exit(pkt_mon_exit);
MODULE_LICENSE("GPL");
