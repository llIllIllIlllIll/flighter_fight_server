# include <arpa/inet.h>
# include <sys/socket.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <netdb.h>
# include <string.h>
# include <unistd.h>
# include <stdio.h>
# include <errno.h>
# include <stdlib.h>
# include <pthread.h>
# include <semaphore.h>
# define MAXLINE 128
# define RIO_BUFSIZE 8192
# define LISTENQ 1024
# define LLL 60
typedef struct sockaddr SA;
typedef struct {
    int rio_fd;
    int rio_cnt;
    char * rio_bufptr;
    char rio_buf[RIO_BUFSIZE];
} rio_t;
void rio_readinitb(rio_t *rp,int fd){
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}
ssize_t rio_writen(int fd,void *usrbuf,size_t n){
    size_t nleft = n;
    ssize_t nwritten;
    char * bufp = usrbuf;
    while(nleft > 0){
        if((nwritten = write(fd,bufp,nleft))<=0){
            if(errno = EINTR)
                nwritten = 0;
            else
                return -1;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}
static ssize_t rio_read (rio_t *rp , char *usrbuf , size_t n) 
{ 
    int cnt;
    while(rp->rio_cnt <= 0){
        rp->rio_cnt = read(rp->rio_fd,rp->rio_buf,sizeof(rp->rio_buf));
        if(rp->rio_cnt < 0){
            if(errno != EINTR)
                return -1;
        }
        else if(rp->rio_cnt == 0){
            return 0;
        }
        else
        {
            rp->rio_bufptr = rp->rio_buf;
        }
    } 
    cnt = n;
    if(rp->rio_cnt < n)
        cnt = rp->rio_cnt;
    memcpy(usrbuf,rp->rio_bufptr,cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -=cnt;
    return cnt;
}
ssize_t rio_readlineb(rio_t *rp,void *usrbuf,size_t maxlen){
    int n,rc;
    char c, *bufp = usrbuf;
    for(n= 1;n<maxlen;n++){
        if((rc = rio_read(rp,&c,1)) == 1){
            *bufp++ = c;
            if(c == '\n'){
                n++;
                break;
            }
        }
        else if(rc == 0){
            if(n == 1)
                return 0;
            else 
                break;
        }
        else
        {
            return -1;
        }
    }
    *bufp = 0;
    return n-1;
}
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n){
    size_t nleft = n;
    ssize_t nread;
    char * bufp = usrbuf;
    while(nleft > 0){
        if((nread = rio_read(rp,bufp,nleft))<0)
            return -1;
        else if(nread == 0)
            continue;
        nleft -= nread;
        bufp += nread;
    }
    return (n-nleft);
}
int open_clientfd(char * hostname, char * port){
    int clientfd;
    struct addrinfo hints, *listp, *p;
    int listenfd, optval = 1;
    memset(&hints,0,sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_flags |= AI_NUMERICSERV;
    getaddrinfo(hostname,port,&hints,&listp);
    for(p = listp; p; p = p->ai_next){
        if((clientfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0) continue;
        if(connect(clientfd,p->ai_addr,p->ai_addrlen)!=-1)  break;
        close(clientfd);
    }
    freeaddrinfo(listp);
    if(!p)
        return -1;
    else
    {
        return clientfd;
    }
    
}
int open_listenfd(char * port){
    struct addrinfo hints,*listp,*p;
    int listenfd,optval = 1;
    memset(&hints,0,sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    hints.ai_flags |= AI_NUMERICSERV;
    getaddrinfo("0.0.0.0",port,&hints,&listp);
    for(p = listp;p;p=p->ai_next){
        if((listenfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0) continue;
        setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,(const void *)&optval,sizeof(int));
        if(bind(listenfd,p->ai_addr,p->ai_addrlen) == 0)   break;
        close(listenfd);
    }
    freeaddrinfo(listp);
    if(!p)
        return -1;
    if(listen(listenfd,LISTENQ)<0){
        close(listenfd);
        return -1;
    }
    return listenfd;
}

typedef struct{
    int * buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;

}sbuf_t;
void sbuf_init(sbuf_t *sp,int n){
    sp->buf = calloc(n,sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    sem_init(&sp->mutex,0,1);
    sem_init(&sp->slots,0,n);
    sem_init(&sp->items,0,0);
}
void sbuf_deinit(sbuf_t *sp){
    free(sp->buf);
}
void sbuf_insert(sbuf_t *sp,int item){
    sem_wait(&sp->slots);
    sem_wait(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)] = item;
    sem_post(&sp->mutex);
    sem_post(&sp->items);
}
int sbuf_remove(sbuf_t * sp){
    int item;
    sem_wait(&sp->items);
    sem_wait(&sp->mutex);
    item = sp->buf[(++sp->front)%(sp->n)];
    sem_post(&sp->mutex);
    sem_post(&sp->slots);
    return item;
}
// parse request headers: ignore them all
void read_requesthdrs(rio_t * rp){
	char buf[MAXLINE];
	rio_readlineb(rp,buf,MAXLINE);
	printf("header line:%s",buf);
	while(strcmp(buf,"\r\n")){
		rio_readlineb(rp,buf,MAXLINE);		
		printf("header line:%s",buf);
	}
	return;

}
