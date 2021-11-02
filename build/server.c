#include <stdlib.h>
#include <stdint.h>
#include <net/if.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netinet/if_ether.h>
#include <net/if_arp.h>
#include <asm/byteorder.h>
#include <byteswap.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <time.h>

#include <libmemif.h>
#include <memif.h>
#include <memif_private.h>

#define APP_NAME "server"
#define IF_NAME "my_memif"
#define MAX_MEMIF_BUFS 256

typedef struct
{
  uint16_t index;
  /* memif conenction handle */
  memif_conn_handle_t conn;
  /* transmit queue id */
  uint16_t tx_qid;
  /* tx buffers */
  memif_buffer_t *tx_bufs;
  /* allocated tx buffers counter */
  /* number of tx buffers pointing to shared memory */
  uint16_t tx_buf_num;
  /* rx buffers */
  memif_buffer_t *rx_bufs;
  /* allcoated rx buffers counter */
  /* number of rx buffers pointing to shared memory */
  uint16_t rx_buf_num;
  /* interface ip address */
  uint8_t ip_addr[4];
} memif_connection_x;

typedef struct 
{
    memif_socket_handle_t sock;
    memif_socket_args_t args;
}sock_t;

sock_t sock;
memif_connection_x memif_connection;
int is_connected=0;

int
on_connect(memif_conn_handle_t conn, void* private_ctx)
{
    printf("memif connected!\n");
    is_connected=1;
    memif_connection_t* c=(memif_connection_t*)conn;
    for(int i=0;i<c->rx_queues_num;i++)
    {
        int err=memif_refill_queue(conn,i,-1,0);
        printf("%s\n", strerror(err));
    }

}

int
on_disconnect(memif_conn_handle_t conn, void* private_ctx)
{
    printf("memif disconnected!\n");
    is_connected=0;
}

int
on_interrupt(memif_conn_handle_t conn, void* private_ctx, uint16_t qid)
{
    printf("memif interrupted!\n");
}

int
receive_data(uint16_t qid)
{
    uint16_t rx;
    memif_connection_x* mc=&memif_connection;
    int err;
    err=memif_rx_burst(mc->conn,qid,mc->rx_bufs,MAX_MEMIF_BUFS,&rx);
    mc->rx_buf_num+=rx;
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("receive adata failed! %d\n", err);
        return -1;
    }

    for(int i=0;i<rx;i++)
    {
        char* data=(char*)mc->rx_bufs[i].data;
        printf("this is recieved data: %s\n",data);
    }
    /* refill queue and inform peer client that memory is free */
    err=memif_refill_queue(mc->conn,qid,rx,0);
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("memif refill queue failed!\n");
        return -1;
    }
    mc->rx_buf_num-=rx;
    return rx>0;
}


int
send_data(uint16_t qid, int count)
{
    uint16_t tx;
    memif_connection_x* mc=&memif_connection;
    /* allocate buffer for tx_Q*/
    int err;
    uint16_t r;

    for(int i=0;i<5;i++)
    {
        err=memif_refill_queue(mc->conn,i,-1,0);
        if(err!=MEMIF_ERR_SUCCESS)
        {
            printf("memif refill queue failed!\n");
            return -1;
        }
    }

    err=memif_buffer_alloc(mc->conn,qid,mc->tx_bufs,count,&r,sizeof(char)*20);
    mc->tx_buf_num+=r;
    strncpy((char*)mc->tx_bufs[0].data,"hi, client!\n", 20);
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("memif allocate buffer failed!\n");
        return 1;
    }

    /* send data*/
    err=memif_tx_burst(mc->conn,qid,mc->tx_bufs,mc->tx_buf_num,&tx);
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("send data failed!\n");
        return 1;
    }
    mc->tx_buf_num-=tx;
    return 0;
}

int 
main()
{
    /* init socket args and create socket */
    strncpy(sock.args.app_name,APP_NAME,sizeof(sock.args.app_name));
    strncpy(sock.args.path,"/home/qinyong/vpp/run/my.socket",sizeof(sock.args.path));
    sock.args.alloc=NULL;
    sock.args.realloc=NULL;
    sock.args.free=NULL;
    sock.args.on_control_fd_update=NULL;

    /* create socket */
    int err;
    err=memif_create_socket(&sock.sock,&sock.args,NULL);
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("create socket failed!\n");
        return 1;
    }

    /* init memif args */
    memif_conn_args_t args;
    args.buffer_size=4096;
    args.num_m2s_rings=2;
    args.num_s2m_rings=2;
    args.socket=sock.sock;
    args.log2_ring_size=10;
    args.mode=0;
    strncpy((char*)args.interface_name,IF_NAME,sizeof(IF_NAME));
    args.interface_id=2;
    args.is_master=1;

    memif_connection_x* mc=&memif_connection;
    mc->conn=NULL;

    mc->tx_qid=1;
    mc->rx_buf_num=0;
    mc->rx_bufs=(memif_buffer_t*)malloc(sizeof(memif_buffer_t)*MAX_MEMIF_BUFS);
    mc->tx_buf_num=0;
    mc->tx_bufs=(memif_buffer_t*)malloc(sizeof(memif_buffer_t)*MAX_MEMIF_BUFS);

    /* create memif */
    err=memif_create(&(mc->conn),&args,on_connect,on_disconnect,on_interrupt,NULL);
    if(err!=MEMIF_ERR_SUCCESS)
    {
        printf("create memif failed!\n");
        return 1;
    }

    memif_connection_t* mct=(memif_connection_t*)mc->conn;
    printf("%d\n",mct->rx_queues_num);
    if(mct->control_channel==NULL){
        printf("yes!\n");
    }

    while (1)
    {
        if (memif_poll_event(sock.sock,-1)<0)
        {
            printf("event poll failed!\n");
            return 1;
        }
        else
        {
            if(is_connected==1){
                receive_data(0);
            }
        }
    }
    

}
