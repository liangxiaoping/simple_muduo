#include <sys/socket.h>  
#include <sys/epoll.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  
#include <fcntl.h>  
#include <unistd.h>  
#include <stdio.h>  
#include <errno.h>

#include <vector>
#include <string>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>


const int kMaxEvent            = 500;
const int kInitialBufferSize   = 256;

const int kLengthOfListenQueue = 5;
const int kEpollTimetOut       = 1000;
const int kInitEventListSize   = 16;

const int kNew     = -1;
const int kAdded   = 1;
const int kDeleted = 2;

class Channel;
typedef boost::shared_ptr<Channel> ChannelPtr;
typedef boost::function<void()> EventCallback;
typedef std::vector<struct epoll_event> EventList;
typedef std::vector<ChannelPtr> ChannelList;
typedef std::vector<char> Buffer;

class Channel : public boost::noncopyable
{
 public:
  Channel(int sockfd, int pollfd, int evs, int revs,int stas);
  ~Channel();
  int socketFd() { return socketFd_; }
  int epollFd() { return epollFd_; }
  int events() { return events_; }
  int status() { return status_; }
  Buffer* inputBuffer() { return &inputBuf_; }
  Buffer* outputBuffer() { return &outputBuf; }
  
  void setevents(int ev) { events_ = ev; }
  void setrevents(int ev) { revents_ = ev; }
  void setStatus(int s) { status_ = s; }
  void setReadCallback(const EventCallback& cb) { readCallback_ = cb; }
  void setWriteCallback(const EventCallback& cb) { writeCallback_ = cb; }
  void setCloseCallback(const EventCallback& cb) { closeCallback_ = cb; }
  void setErrorCallback(const EventCallback& cb) { errorCallback_ = cb; }

  void handleEvent();
  void printInfo();
 private:
  
  int     socketFd_;
  int     epollFd_;
  int     events_;    // for epoll_ctl
  int     revents_;   // from epoll_wait
  int     status_;
  Buffer  inputBuf_;
  Buffer  outputBuf;
  EventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

Channel::Channel(int sockfd, int pollfd, int evs, int revs,int stas)
  : socketFd_(sockfd),
    epollFd_(pollfd),
    events_(evs),
    revents_(revs),
    status_(stas),
    inputBuf_(kInitialBufferSize),
    outputBuf(kInitialBufferSize)
{
  printf("Enter function: %s.\r\n", "Channel::Channel()");
}

Channel::~Channel()
{
  if(socketFd_ > 0)
  {
    ::close(socketFd_);
  }
  printf("Enter function: %s.\r\n", "Channel::~Channel()");
}

void Channel::handleEvent()
{
  printf("Enter function: %s.\r\n", "Channel::handleEvent()");
  
  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
  {
    if (readCallback_) readCallback_();
  }
  
  if (revents_ & EPOLLOUT)
  {
    if (writeCallback_) writeCallback_();
  }
  
  if ((revents_ & EPOLLHUP) && !(events_ & EPOLLIN))
  {
    if (closeCallback_) closeCallback_();
  }

  if (revents_ & (EPOLLERR))
  {
    if (errorCallback_) errorCallback_();
  }
}


void Channel::printInfo()
{
  printf("socketFd       = %d . \r\n", socketFd_);
  printf("pollerFd       = %d . \r\n", epollFd_);
  printf("events         = %d . \r\n", events_);
  printf("revents        = %d . \r\n", revents_);
  printf("status         = %d . \r\n", status_);
}

void epollctl(int op, const ChannelPtr &channel)
{
  printf("Enter function: %s.\r\n", __func__);
  
  struct epoll_event epv = {0, {0}};   
  epv.data.ptr = channel.get();
  epv.events   = channel->events();

  if(::epoll_ctl(channel->epollFd(), op, channel->socketFd(), &epv) < 0)
  {
    printf("epoll_ctl failed[epollFd = %d, socketFd = %d]\n", channel->epollFd(), channel->socketFd()); 
  }
}

// EPOLL_CTL_ADD/MOD
void updateChannel(const ChannelPtr &channel)
{
  printf("Enter function: %s.\r\n", __func__);
  
  if(channel->status() == kAdded)
  {
    epollctl(EPOLL_CTL_MOD, channel);
  }
  else
  {
    epollctl(EPOLL_CTL_ADD, channel);
    channel->setStatus(kAdded);
  }
}

// EPOLL_CTL_DEL
void removeChannel(const ChannelPtr &channel)
{
  printf("Enter function: %s.\r\n", __func__);
  epollctl(EPOLL_CTL_DEL, channel);
  channel->setStatus(kDeleted);
}

void closeFd(int Fd)
{
  if(Fd > 0)
  {
    ::close(Fd);
  }
}

bool setNonBlock(int socketFd)
{
  // non-block
  int flags = ::fcntl(socketFd, F_GETFL, 0);
  
  flags |= O_NONBLOCK;
  
  int ret = ::fcntl(socketFd, F_SETFL, flags);

  return (ret < 0) ? false : true ;
}

bool closeOnExec( int socketFd )
{
  // close-on-exec
  int flags = ::fcntl(socketFd, F_GETFD, 0);
  
  flags |= FD_CLOEXEC;
  
  int ret = ::fcntl(socketFd, F_SETFD, flags);

  return (ret < 0) ? false : true ;
}

bool setReuseAddr(int socketFd, bool on)
{
  int optval = on ? 1 : 0;
  
  int ret = ::setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  return (ret < 0) ? false : true ;
}

void readCallBack(const ChannelPtr &channel);  
void writeCallBack(const ChannelPtr &channel);
void closeCallBack(const ChannelPtr &channel);
void errorCallBack(const ChannelPtr &channel);
  
// accept new connections from clients  
void connectionCallBack(const ChannelPtr &channel, ChannelList *channels)  
{
  printf("Enter function: %s.\r\n", __func__);

  if(channels == NULL)
  {
    return;
  }

  struct sockaddr_in sin; 
  memset(&sin,  0, sizeof(sin));
  
  socklen_t len = sizeof(struct sockaddr_in);  
  int clientfd = -1;
  
  // accept  
  if((clientfd = accept(channel->socketFd(), (struct sockaddr*)&sin, &len)) < 0)  
  { 
    switch (errno)
    {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO:
      case EPERM:
      case EMFILE:
        printf("error of ::accept, errno = %d.\r\n", errno);
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        printf("unexpected error of ::accept, errno = %d.\r\n", errno);
        break;
      default:
        printf("unknown error of ::accept, errno = %d.\r\n", errno);
        break;
    }
    return;  
  }
  
  // set non-blocking
  if(!setNonBlock(clientfd))
  {
     return;
  }

  // set non-blocking
  if(!closeOnExec(clientfd))
  {
     return;
  }

  // LT
  ChannelPtr connChannel(new Channel(clientfd,channel->epollFd(),EPOLLIN|EPOLLPRI,0,kNew));

  connChannel->setReadCallback(boost::bind(readCallBack, connChannel));
  updateChannel(connChannel);
  channels->push_back(connChannel);
  
  printf("new conn[fd:%d][%s:%d]\r\n", clientfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

}  

void readCallBack(const ChannelPtr &channel)
{  
  printf( "Enter function: %s.\r\n", __func__ );

  Buffer * inputBuf = channel->inputBuffer();
  // receive data  
  int rlen = read(channel->socketFd(), &*inputBuf->begin(), inputBuf->size());
  
  if(rlen > 0)
  {
    // messageCallBack();
    std::string msg(&*inputBuf->begin(), rlen);
    printf("socket[%d]:%s\n", channel->socketFd(), msg.c_str());

    int wlen = write(channel->socketFd(), &*inputBuf->begin(), rlen);
    if(wlen > 0 && wlen < rlen)
    {
      // append to outputbuffer and enable check EPOLLOUT event
    }
  }
  else if(rlen == 0)  
  {  
    closeCallBack(channel);
  }  
  else  
  {
    errorCallBack(channel);
  }  
}
  
// TCP client send data to server ( after call connect )
void writeCallBack(const ChannelPtr &channel)  
{  
  printf("Enter function: %s.\r\n", __func__);
      
  Buffer * outputBuf = channel->outputBuffer();
  
  // send data  
  int len = write(channel->socketFd(), &*outputBuf->begin(), outputBuf->size());
  if(len < 0)
  {  
    errorCallBack(channel);
  }

  //if(len == outputBuf->size())
  //{
    // if outputbuffer have no data, disable check EPOLLOUT
    // int disablewrite = channel->events();
    // disablewrite &= ~EPOLLOUT;
    // updateChannel(channel);
  //}
  
}

void closeCallBack(const ChannelPtr &channel)
{
  printf("Enter function: %s.\r\n", __func__);

  channel->setevents(0);
  removeChannel(channel);
  closeFd(channel->socketFd());
  
  printf( "Connections closed, socketfd = %d.\r\n",channel->socketFd());
}

void errorCallBack(const ChannelPtr &channel)
{
  printf("Enter function: %s.\r\n", __func__);

  int optval;
  socklen_t optlen = sizeof(optval);
  int err = 0;
  if(getsockopt(channel->socketFd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) <0)
  {
    err = errno;
  }
  else
  {
    err = optval;
  }

  printf("Connection closed[err = %d, %s].\r\n", err, strerror(errno));
}

bool openTCPServer( int &listenFd,  short port )  
{  
  printf("Enter function: %s.\r\n", __func__);

  listenFd = ::socket(AF_INET, SOCK_STREAM, 0);

  if(listenFd < 0)
  {
    return false;
  }

  // set non-blocking
  if(!setNonBlock(listenFd))
  {
    return false;
  }

  // close on exec
  if(!closeOnExec(listenFd))
  {
    return false;
  }

  // set reuse
  if(!setReuseAddr(listenFd,true))
  {
    return false;
  }

  // bind
  sockaddr_in sin;  
  memset(&sin,  0, sizeof(sin));  
  sin.sin_family      = AF_INET;  
  sin.sin_addr.s_addr = INADDR_ANY;  
  sin.sin_port        = htons(port);

  int ret = -1;
  ret = ::bind(listenFd, (const sockaddr*)&sin, sizeof(sin));

  if( ret < 0)
  {
    return false;
  }

  // listen
  ret = ::listen(listenFd, kLengthOfListenQueue); 

  if(ret < 0)
  {
    return false;
  }

  printf("server listen fd = %d . \r\n", listenFd); 

  return true;
}


// one loop per thread
int main(int argc, char **argv)  
{  
  short port = 12345;
  
  if(argc == 2)
  {  
    port = atoi(argv[1]);  
  }

  int listenFd = -1;
  // open a server listening on the port 
  if( !openTCPServer(listenFd, port))
  {
    closeFd(listenFd);
    return 2;
  }
  
  printf("server running on port[%d].\r\n", port);

  int epollFd = ::epoll_create( kMaxEvent );  
  if( epollFd <= 0 ) 
  {
    printf("create epoll failed. epollFd = %d.\r\n", epollFd); 
    closeFd(listenFd);
    return 3;
  }

  ChannelList channels;

  // LT
  ChannelPtr acceptChannel(new Channel(listenFd,epollFd,EPOLLIN|EPOLLPRI,0,kNew));
  acceptChannel->setReadCallback(boost::bind(connectionCallBack, acceptChannel, &channels));
  updateChannel(acceptChannel);

  channels.push_back(acceptChannel);

  // events  
  EventList events(kInitEventListSize);
  
  while(1)
  {  
    // wait for events to happen  
    int fds = epoll_wait( epollFd, &events[0], kMaxEvent, kEpollTimetOut );  
    if( fds < 0 )
    {  
      printf("epoll_wait error, exit. error[%d]: %s.\r\n", errno, strerror(errno));
      break;  
    }
    
    for(int i = 0; i < fds; i++)
    {  
      Channel* channel = static_cast<Channel*>(events[i].data.ptr);

      // handle event
      if(channel != NULL)
      {
        channel->setrevents(events[i].events);
        channel->handleEvent();
      } 
    }  
  }
  
  // free resource
  for(ChannelList::iterator it = channels.begin(); it != channels.end(); ++it)
  {
    ChannelPtr ch = *it;
    (*it).reset();
    removeChannel(ch);
    ch.reset();
  }
  closeFd(epollFd);
  
  return 0;  
}  

