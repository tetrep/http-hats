#include "tunnel.hpp"

tunnel::~tunnel()
{
  std::cout << "~tunnel()" << std::endl;

  if(work != NULL){io_service->stop(); delete work;}
  if(remote_socket != NULL){if(remote_socket->is_open()){remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive); remote_socket->close();} delete remote_socket;}
  if(local_socket != NULL){if(local_socket->is_open()){local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive); local_socket->close();} delete local_socket;}
  if(io_service != NULL){delete io_service;}
  if(local_buffer != NULL){delete local_buffer;}
  if(remote_buffer != NULL){delete remote_buffer;}
  if(local_mutex != NULL){delete local_mutex;}
  if(remote_mutex != NULL){delete remote_mutex;}
}

tunnel::tunnel(boost::asio::ip::tcp::socket *socket, char *remote, char *remote_port, char clientserver, char *the_header, char *the_tail,
    std::size_t header_size, std::size_t tail_size, unsigned int receive_buffer_size) throw()
{
  //nulls
  null();

  try
  {
    //im not dead yet
    alive.lock();
    //set up passed in variables and buffers
    this->the_header = the_header;
    this->the_tail = the_tail;
    this->header_size = header_size;
    this->tail_size = tail_size;
    this->receive_buffer_size = receive_buffer_size;
    this->clientserver = clientserver;

    //set up buffers
    local_buffer = new char[receive_buffer_size];
    remote_buffer = new char[receive_buffer_size];

    memset(local_buffer, 'r', receive_buffer_size);
    memset(remote_buffer, 'r', receive_buffer_size);

    //set up mutexes
    local_mutex = new boost::mutex();
    remote_mutex = new boost::mutex();

    //set up io_service
    io_service = new boost::asio::io_service();

    //will resolve remote address
    boost::asio::ip::tcp::resolver resolver(*io_service);

    //build query to feed resolver
    boost::asio::ip::tcp::resolver::query query(remote, remote_port);

    //resolve!
    boost::asio::ip::tcp::resolver::iterator endpoints = resolver.resolve(query);

    //set up encrypt/decrypt and local/remote sockets 
    if(clientserver == 'c')
    {
      local_bool = false;
      remote_bool = true;

      local_socket = socket;
      remote_socket = new boost::asio::ip::tcp::socket(*io_service);

      //receive less to be sure we can send full message with header and tail attached
      local_receive_size = receive_buffer_size - header_size - tail_size;
      remote_receive_size = receive_buffer_size;
    }
    else if (clientserver == 's')
    {
      local_bool = true;
      remote_bool = false;

      local_socket = socket;
      remote_socket = new boost::asio::ip::tcp::socket(*io_service);

      //receive less to be sure we can send full message with header and tail attached
      local_receive_size = receive_buffer_size;
      remote_receive_size = receive_buffer_size - header_size - tail_size;
    }

    //connect!
    boost::asio::connect(*remote_socket, endpoints);
  }
  catch(std::exception &e)
  {
    std::cout << "=====ERROR=====" << std::endl
      << "Tunnel failed to initialize" << std::endl;

    if(remote_socket != NULL){remote_socket->close(); delete remote_socket;}
    if(local_socket != NULL){local_socket->close(); delete local_socket;}
    if(io_service != NULL){delete io_service;}
    if(local_buffer != NULL){delete local_buffer;}
    if(remote_buffer != NULL){delete remote_buffer;}

    throw e;
  }
}

void tunnel::halt()
{
  std::cout << "Halt!" << std::endl;

  boost::mutex::scoped_lock the_lock(*local_mutex);
  boost::mutex::scoped_lock another_lock(*remote_mutex);

  if(local_socket != NULL)
  {
    if(local_socket->is_open())
    {
      local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive);
      local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send);
      local_socket->close();
    }

    delete local_socket;
    local_socket = NULL;
  }
  if(remote_socket != NULL)
  {
    if(remote_socket->is_open())
    {
      remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive);
      remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send);
      remote_socket->close();
    }

    delete remote_socket;
    remote_socket = NULL;
  }

  //halt io_service
  io_service->stop();
}

void tunnel::hats(bool decrypt, char **temp, char *buffer, std::size_t &bytes_transferred) throw()
{
  *temp = buffer;
  //return;

  if(header_size > 0 && tail_size > 0)
  {
    //remove header/tailer
    if(decrypt)
    {
      std::cout << "header is: " << header_size << std::endl;

      //make sure packet is big enough
      if(bytes_transferred <= (header_size + tail_size)){std::cerr << "Packet too small" << std::endl; halt(); return;}//throw std::exception();}

      //ignore head and tail
      *temp = &buffer[header_size];
      bytes_transferred -= (header_size + tail_size);

      //print(temp, bytes_transferred);
    }
    //add header/tailer
    else if(bytes_transferred > 0)
    {
      //too much data!
      if(bytes_transferred > (receive_buffer_size - header_size - tail_size)){std::cerr << "Packet too large" << std::endl; throw std::exception();}
  
      for(std::size_t i = (bytes_transferred + header_size) - 1; i >= header_size; i--)
        buffer[i] = buffer[i-header_size];
  
      //add header
      strncpy(buffer, the_header, header_size);
  
      //add tail
      strncpy(&buffer[header_size+bytes_transferred], the_tail, tail_size);
  
      //send more
      bytes_transferred += (header_size + tail_size);
  
      //dont care about you
      *temp = buffer;

  //    print(temp, header_size);
  //    print(&temp[header_size+bytes_transferred], tail_size);
  //    print(&buffer[header_size+bytes_transferred], tail_size);
  //    print(the_tail, tail_size);
    }
  }
  std::cout << "hats out" << std::endl;
}
void tunnel::remote_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred)
{
  try
  {
    if(bytes_transferred == 0){throw std::exception();}

    local_to_send = bytes_transferred;

    std::cout << "hats start remote" << std::endl;

    hats(remote_bool, &remote_temp, remote_buffer, local_to_send);

    std::cout << "hats end remote" << std::endl;

    //grab mutex
    boost::mutex::scoped_lock the_lock(*remote_mutex);

    //make sure other side of tunnel has collapsed (and thus, called halt())
    if(local_socket != NULL && local_socket->is_open())
    {
      //send to local
      local_sent = local_socket->send(boost::asio::buffer(remote_temp, local_to_send));
      if(local_sent != local_to_send){std::cerr << "Failed to send everything" << std::endl; throw std::exception();}
    }
    else{throw std::exception();}
    if(remote_socket != NULL && remote_socket->is_open())
    {
      //listen for more
      remote_socket->async_receive(boost::asio::buffer(remote_buffer, remote_receive_size),
        boost::bind(&tunnel::remote_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }
    else{throw std::exception();}
  }
  catch(std::exception &e)
  {
    std::cerr << "=====ERROR=====" << std::endl
      << "Tunnel Exit Collapsed" << std::endl
      << e.what() << std::endl;

    halt();
  }
}

void tunnel::local_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred)
{
  try
  {
    if(bytes_transferred == 0){throw std::exception();}

    remote_to_send = bytes_transferred;

    std::cout << "hats start local" << std::endl;

    hats(local_bool, &local_temp, local_buffer, remote_to_send);

    std::cout << "hats end local" << std::endl;

    //grab mutex
    boost::mutex::scoped_lock the_lock(*local_mutex);

    if(remote_socket != NULL && remote_socket->is_open())
    {
      //send to remote
      remote_sent = remote_socket->send(boost::asio::buffer(local_temp, remote_to_send));
      if(remote_sent != remote_to_send){std::cerr << "Failed to send everything" << std::endl; throw std::exception();}
    }
    else{throw std::exception();}
    if(local_socket != NULL && local_socket->is_open())
    {
      //listen for more
      local_socket->async_receive(boost::asio::buffer(local_buffer, local_receive_size), 
        boost::bind(&tunnel::local_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }
    else{throw std::exception();}

  }
  catch(std::exception &e)
  {
    std::cerr << "=====ERROR=====" << std::endl
      << "Tunnel Entrance Collapsed" << std::endl
      << e.what() << std::endl;

    halt();
  }
}
void tunnel::run()
{
  try
  {
    //add work to io_service so run will not exit 
    work = new (nothrow) boost::asio::io_service::work(*io_service);

    //check for allocation
    if(work == NULL){std::cerr << "Error allocating work" << std::endl;}
  
    //thread io_service.run
    boost::thread io_service_thread(boost::bind(&boost::asio::io_service::run, io_service));

    //watch for incoming data
    local_socket->async_receive(boost::asio::buffer(local_buffer, local_receive_size), 
    boost::bind(&tunnel::local_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    remote_socket->async_receive(boost::asio::buffer(remote_buffer, remote_receive_size),
      boost::bind(&tunnel::remote_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    //wait for thread
    io_service_thread.join();
    std::cout << "we're done!" << std::endl;

    alive.unlock();
  }
  catch(std::exception &e)
  {
    std::cout << "=====ERROR=====" << std::endl
      << "Tunnel failed to run" << std::endl
      << e.what() << std::endl;

      halt();
      alive.unlock();
  }
}
//make all pointers NULL
void tunnel::null()
{
  io_service = NULL;
  remote_socket = local_socket = NULL;
  work = NULL;
  local_mutex = remote_mutex = NULL;
  local_buffer = remote_buffer = the_header = the_tail = NULL;
}
//loop-de-loop
void tunnel::print(char *buffer, std::size_t length)
{
  std::cout << "begin" << std::endl;

  for(std::size_t i = 0; i < length; i++)
    std::cout << buffer[i];

  std::cout << std::endl << "end" << std::endl;
}
