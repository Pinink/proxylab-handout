#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
/*
 * 简单的proxy 支持多线程和有一定缓存， 运用读者-写者模型保证共享，用一个数组去缓存合理的数据*/
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_Connection_hdr = "Proxy-Connection: close\r\n";
/*

*/
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_NUM  1000   //缓存数组的条目个数
typedef struct{
    int valid ;
    char tag[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    int recently_used;
    int object_size;
} block;
//一个缓存条目，用valid标记是否缓存，tag为对于uri object为缓存的对象
block cache[MAX_NUM];
sem_t add_mutex,w_mutex;
void init_cache();
block * find_cache(char * command);
void update_used(int i);
block * find_replace_block();
void replace_block( block * new);
void doit(int fd);
int parse_uri(char *uri, char *host_name, char * port , char * path);
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg);
void read_request_header(rio_t *rp,char * host_header,char * append_header);
void build_request(char * command_line,char * path ,
      char * host_name , char * host_header,char * append_header);
void * thread(void * vargp);
void before_read();
void after_read();
static int current_time = 0;
static int readcnt = 0;
static int cache_size ;
//对于缓存数组和信号量进行初始化
void init_cache(){

    
    cache_size = 0;
    Sem_init(&add_mutex,0,1);
    Sem_init(&w_mutex,0,1);
    int i;
    for( i = 0; i < MAX_NUM ; ++i ){
        cache[i].tag[0] = '\0';
        cache[i].object[0] = '\0';
        cache[i].recently_used = 0;
        cache[i].object_size = 0;
        cache[i].valid = 0;
    }
}
/*判断对应的uri是否命中，如果命中返回对应块的指针
 不命中返回NULL*/
block * find_cache(char * command){

    int num = MAX_NUM;
    int hit = 1;
    int i;
    for(i = 0; i < num ; ++ i){

        if(!cache[i].valid)
            continue;
        if(strlen(cache[i].tag) != strlen(command))continue;
        int j;
        for(j = 0; cache[i].tag[j]!= '\0';++ j){
            
            
            if(cache[i].tag[j] != command[j])
            {
                hit = 0;break;
            }

        }   
            
            if(!hit)
            {   
                hit = 1;
                continue;
            }

            update_used(i);
            return &cache[i];
        
    }
    return NULL;
}
/*更新最新使用的时间，用于lru策略，
由于不需要严格的lru策略，所以不必对该共享变量加锁*/
void update_used(int i){

    
    current_time ++;
    cache[i].recently_used = current_time;
    ;
    
}
/* 找到合理的放下此次条目的位置*/
block * find_replace_block(){

    int index = 0;
    int last_recently = 0xfffffff, replace_number = 0;
    while(index < MAX_NUM){

        if(cache[index].recently_used == 0)
            {
                cache[index].recently_used = current_time ++;
                return &cache[index];
            }
        if(last_recently > cache[index].recently_used){
            last_recently = cache[index].recently_used;
            replace_number = index;
        }

        index ++;
    }
    cache[replace_number].recently_used = current_time ++;
    return &cache[replace_number];

}
/* 当缓存的大小大于MAX_CACHE_SIZE时
* 需要选择一些最不常使用的块驱逐掉，返回值为该块的缓存物体大小*/
int evict_block(){
    
    int index = 0;
    int last_recently = 0xfffffff, replace_number = 0;
     while(index < MAX_NUM){
        if((last_recently > cache[index].recently_used)&&(cache[index].recently_used!=0)){
            last_recently = cache[index].recently_used;
            replace_number = index;
        }

        index ++;
    }
    cache[replace_number].recently_used = 0;
    cache[replace_number].valid = 0;
    
    return cache[replace_number].object_size;
}
/* 将新的缓存块放到数组中，如果当前已缓存大小大于MAX_CACHE_SIZE 
调用evict_block去选择最不常用的块去掉
*/
void replace_block(block * new){  

    cache_size += new->object_size;
    while(cache_size >= MAX_CACHE_SIZE){
        cache_size -= evict_block();
    }
    block * old = find_replace_block();
    strcpy(old->tag,new->tag);
    memcpy(old->object,new->object,new->object_size);
    old -> object_size = new -> object_size;
    old->valid = 1;
    free(new);
    
}
int main(int argc, char **argv) 
{
    int listenfd;
    int * connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    signal(SIGPIPE,SIG_IGN);
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    /* Check command line args */
    if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
    }
    init_cache();
    listenfd = Open_listenfd(argv[1]);
    while (1) {

        clientlen = sizeof(clientaddr);
        connfdp = (int *) Malloc(sizeof(int));
       *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp); 
                                                
    }
    close(listenfd);
    return 0;
}
/* 开一个新线程，分离后调用doit去服务客户*/
void * thread(void * vargp){

    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}
/*读者进入，如果是第一个读者，加写锁保证不会有写者写入*/
void before_read(){
    P(&add_mutex);
    readcnt++;
    if(readcnt == 1)
        P(&w_mutex);
    V(&add_mutex);
}
/*读者离开，如果是最后一个读者，解开写锁使得可以有写者写入*/
void after_read(){
    P(&add_mutex);
    readcnt--;
    if(readcnt == 0)
        V(&w_mutex);
    V(&add_mutex);
}
/* 从客户端读入数据并分析，打开对应的服务器端并传送相应命令
 * 再将从服务器端得到的数据返回给客户端*/
void doit(int client_fd){


    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host_header[MAXLINE],append_header[MAXLINE];
    char host_name[MAXLINE],path[MAXLINE],port[MAXLINE];
    char command_line[MAXLINE];
    rio_t client_rio,server_rio;

    int server_fd;
    Rio_readinitb(&client_rio,client_fd);
    if(!Rio_readlineb(&client_rio,buf,MAXLINE))
        return ;
    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);       
    if (strcasecmp(method, "GET")) {                     
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
    
    if( parse_uri(uri,host_name,port,path)== -1)
    {
        clienterror(client_fd, uri, "404", "Not found",
            "Tiny couldn't find this file");
        return;
    } 
    read_request_header(&client_rio,host_header,append_header); 
    build_request(command_line,path,host_name,host_header,append_header);

   

    /* 在读之前加锁*/
    before_read();
    
    block * cache_block  =  find_cache(uri);
    if(cache_block != NULL)
    {   
            

          Rio_writen(client_fd,cache_block->object,cache_block->object_size);
          after_read();
            /* 结束时解锁*/
          return ;
    }
    else
    {
        after_read();/*解锁*/
        cache_block = Malloc(sizeof(block));
        cache_block->object_size = 0;
        strcpy(cache_block->tag,uri);
    }

    server_fd = Open_clientfd(host_name,port);

    
    Rio_readinitb(&server_rio,server_fd);
    Rio_writen(server_fd,command_line,strlen(command_line));
     
    int read_count = 0;

    char * temp_buf_point = cache_block->object;

    while((read_count = Rio_readnb(&server_rio,temp_buf_point,MAXLINE)) > 0){

       Rio_writen(client_fd,temp_buf_point,read_count);

       cache_block->object_size += read_count;

       if(cache_block->object_size < MAX_OBJECT_SIZE)
        {
            temp_buf_point += read_count;
       }
       else
       {
           temp_buf_point = cache_block->object;
       }
    }
    if(cache_block->object_size < MAX_OBJECT_SIZE)
    { 
        P(&w_mutex);
        replace_block(cache_block);
        V(&w_mutex);
    }
    close(server_fd);
}
/*分析uri*/
int parse_uri(char *uri, char *host_name, char * port , char * path)
{
    if(strncmp(uri,"http://",strlen("http://"))){
       return -1;
    }

    uri += strlen("http://");

    /* 默认端口为80*/
    port[0]='8';
    port[1]='0';
    port[2]='\0';
    int i = 0;
    /* 从第一个开始找到 :  或者 /字符 之前的为host name和端口号(可能没有) */
    for(i = 0; uri[i] !='\0'; ++i ){
        if(uri[i] == ':')
        {
           
            if(i == 0){ printf("error\n");return -1;}
            

            int j = 0;
             for( j = 0 ;uri[j]!= ':';++j){
                host_name[j]=uri[j];
             }

             host_name[j] = '\0';
             int p = 0 ;
             for( j = j + 1 ;uri[j]!= '/'; ++j){
                port[p++] = uri[j];
             }
             port[p] = '\0';

            
            if(uri[i] != '/'){ printf("160 uri[i] != '/' error!\n");}
           
            
             i = j ;
             break;
        }
        else if(uri[i] == '/'){

             int j = 0;
             for( j = 0 ;uri[j]!= '/';++j){
                host_name[j]=uri[j];
             }
             host_name[j] = '\0';
             i = j ;
             break;
        }
    }
    uri += i;
    strcpy(path,uri);
    return 1;
}
/*读入请求头 ，如果有hostname则在之后用，不然用之前uri中分析得到的hostname
剩下的原样传送给服务器*/
void read_request_header(rio_t *rp,char * host_header,char * append_header) 
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm

        int len = strlen("Host:");
        if(!strncmp(buf,"Host:",len))
        {
            strcpy(host_header,buf);
        }
        else if(!strncmp(buf,"User-Agent:",strlen("User-Agent:")))
        {
                 Rio_readlineb(rp, buf, MAXLINE);
                 printf("%s", buf);
                continue;
        }
        else if(!strncmp(buf,"Connection:",strlen("Connection:")))
        {

            Rio_readlineb(rp, buf, MAXLINE);
            printf("%s", buf);
            continue;
        }
        else if(!strncmp(buf,"Proxy-Connection:",strlen("Proxy-Connection:")))
        {
             Rio_readlineb(rp, buf, MAXLINE);
             printf("%s", buf);
             continue;
        }
        else
        {
            int append_len = strlen(buf);
            strcpy(append_header , buf);
            append_header += append_len ;   
        }
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
}
/* 根据请求行和请求报头构建将发送给服务器的HTTP请求*/
void build_request(char * command_line,char * path , 
    char * host_name , char * host_header,char * append_header)
{
    
    sprintf(command_line,"GET %s HTTP/1.0\r\n",path);
    if(strlen(host_header) != 0)
    {
        sprintf(command_line,"%s%s",command_line,host_header);
    }
    else
        sprintf(command_line,"%s%s\r\n",command_line,host_name);

    sprintf(command_line,"%s%s",command_line,user_agent_hdr);
    sprintf(command_line,"%s%s",command_line,connection_hdr);
    sprintf(command_line,"%s%s",command_line,proxy_Connection_hdr);
    sprintf(command_line,"%s%s",command_line,append_header);
    sprintf(command_line,"%s\r\n",command_line);
}
/* 给客户发送错误信息*/
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
