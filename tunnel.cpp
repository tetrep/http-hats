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

//==================
//what follows is mostly (if not only) used by server
//==================

//our wonderful gc
void tunnel::reap(std::vector <boost::thread *> *threads, std::vector <tunnel *> *tunnels, boost::mutex *reap_mutex,boost::mutex *fin_mutex)
{
  //keep alive
  while(true)
  {
    //dont run too fast
    boost::this_thread::sleep(boost::posix_time::seconds(10));

    //is entrance still up?
    if(fin_mutex->try_lock()){return;}

    //gimme mutex
    boost::mutex::scoped_lock the_lock(*reap_mutex);

    //iterate through threads
    for(std::size_t i = 0; i < (*threads).size(); i++)
    {
      //is thread done?
      if((*tunnels)[i]->is_alive()->try_lock())
      {
        //unlock so we can kill it
        (*tunnels)[i]->is_alive()->unlock();

        //join thread
        (*threads)[i]->join();
  
        //delete thread and tunnel
        delete (*threads)[i];
        delete (*tunnels)[i];
  
        //erase thread pointer and tunnel pointer
        (*threads).erase((*threads).begin() + i);
        (*tunnels).erase((*tunnels).begin() + i);
      }
    }
    
    //release memory if we have more than GARBAGE_RATE bytes allocated but unused by vector
  //  if(((threads.capacity() - threads.size) * sizeof(boost::thread *)) >= GARBAGE_RATE){threads.shrink_to_fit();}
    
  }
}

//read in settings from file
void tunnel::load_settings(char *file, char *clientserver) throw()
{
  try
  {
    int i;
    //does the profile given have a file extension?
    for(i = 0; file[i] != '\0'; i++){if(file[i] == '.'){break;}}

    //no file extension, add .conf
    if(file[i] == '\0')
    {
      char *temp = new char[i+6];
      for(i = 0; file[i] != '\0'; i++){temp[i] = file[i];}
      temp[i] = '.';
      temp[i+1] = 'c';
      temp[i+2] = 'o';
      temp[i+3] = 'n';
      temp[i+4] = 'f';
      temp[i+5] = '\0';

      file = temp;
    }

    std::ifstream settings(file);
    string keyword;

    //set defaults
    char delim = '*';
    *clientserver = 'f';
    header_size = 100;
    tail_size = 100;
    receive_buffer_size = 1024;
    the_header = the_tail = NULL;

    if(!settings.is_open()){throw std::exception();}

    while(!settings.eof())
    {
      settings >> keyword;

      if(keyword[0] == '/' && keyword.size() > 1 && keyword[1] == '/'){continue;}

      if(keyword == "ClientServer:"){     settings >> *clientserver;}
      else if(keyword ==  "HeaderSize:"){   settings >> header_size;}
      else if(keyword == "TailSize:"){    settings >> tail_size;}
      else if(keyword == "Header:"){      the_header = new char[header_size]; 
                  settings.get();
                  settings.get(the_header, header_size, delim);
                  header_size = settings.gcount();}
      else if(keyword ==  "Tail:"){     the_tail = new char[tail_size];
                  settings.get();
                  settings.get(the_tail, tail_size, delim);
                  tail_size = settings.gcount();}
      else if(keyword == "Delimiter:"){   settings >> delim;}
      else if(keyword == "ReceiveBufferSize:"){ settings >> receive_buffer_size;}
    }
    
    std::cout << header_size << std::endl << tail_size << std::endl;
/*
    std::size_t j;

    for(j = 0; j < header_size; j++)
      std::cout << the_header[j];
    std::cout << std::endl;
    for(j = 0; j < tail_size; j++)
      std::cout << the_tail[j];
    std::cout << std::endl;
*/
    settings.close();
  }
  catch(std::exception &e)
  {
    throw e;
  }

/*
    settings >> *clientserver
    //header and tail sizes
      >> header_size >> tail_size
    //receive buffer size
      >> receive_buffer_size;

    //allocate header and tail
    the_header = new (nothrow) char[header_size];
    the_tail = new (nothrow) char[tail_size];

    if(the_header == NULL || the_tail == NULL){throw std::exception();}

    settings.get();

    //read header
    settings.get(the_header, header_size, 'Q');

    settings.get();

    //read tail
    settings.get(the_tail, tail_size, 'Q');

    std::cout << "the header:" << std::endl
      << the_header << std::endl
      << "the tail:" << std::endl
      << the_tail << std::endl
      << "header size:" << std::endl
      << header_size << std::endl
      << "tail size:" << std::endl
      << tail_size << std::endl;

    settings.close();
*/
}

//tunnel server
tunnel::tunnel(int argc, char **argv)
{
  //nulls
  null();

  //reap_thread
  boost::thread *reap_thread = NULL;

  try
  {
    //make sure we hae the correct amount of arguments
    if(argc != 5)
    {
      std::cerr << "Error, correct format is..." << std::endl
        <<"==for client==" << std::endl
          << "   client [local port] [remote host] [remote port]" << std::endl
        << "==for server==" << std::endl 
          << "   server [local port] [remote host] [remote port]" << std::endl;
      return;
    }

    char clientserver;

    load_settings(argv[1], &clientserver);

    //vectors of our threads
    std::vector <boost::thread *> threads;
    std::vector <tunnel *> tunnels;

    //the io_service
    io_service = new boost::asio::io_service();

    //add work to io_service
    boost::asio::io_service::work work = boost::asio::io_service::work(*io_service);

    //set bind and listen to local port

    //build local port
    string str_local_port = "", str_remote_port = "";

    for(int i = 0; argv[2][i] != '\0'; i++)
       str_local_port += argv[2][i];

    //create string stream
    std::stringstream converter(str_local_port);

    //convert string to int
    int local_port;
    converter >> local_port;

    //create acceptor to bind and listen
    boost::asio::ip::tcp::acceptor acceptor(*io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), local_port));

    //thread off io_service
    boost::thread io_service_thread(boost::bind(&boost::asio::io_service::run, io_service));

    //create reap_mutex and fin_mutex
    boost::mutex *reap_mutex = NULL;
    boost::mutex *fin_mutex = NULL;

    reap_mutex = new boost::mutex();
    fin_mutex = new boost::mutex();

    local_mutex = new boost::mutex();
    remote_mutex = new boost::mutex();

    //thread off reap() to kill dead threads, but first, mutex to keep reap alive
    boost::mutex::scoped_lock fin(*fin_mutex);

    reap_thread = new boost::thread(boost::bind(&tunnel::reap, this, &threads, &tunnels, reap_mutex, fin_mutex));

    //tunnel pointer
    tunnel *new_tunnel;

    //thread pointer
    boost::thread *new_thread;

    //main loop for acception new connections
    while(true)
    {
      //make a new socket
      local_socket = new (nothrow) boost::asio::ip::tcp::socket(*io_service);

      //check for allocation
      if(local_socket == NULL){std::cerr << "Error allocating new socket" << std::endl; continue;}

      //wait for incoming connection
      acceptor.accept(*local_socket);

      //create tunnel
      new_tunnel = new (nothrow) tunnel(local_socket, argv[3], argv[4], clientserver, the_header, the_tail, header_size, tail_size, receive_buffer_size);

      //check allocation
      if(new_tunnel == NULL){std::cerr << "Error allocating new tunnel" << std::endl; delete local_socket; continue;}

      //thread and create tunnel
      new_thread = new (nothrow) boost::thread(boost::bind(&tunnel::run, new_tunnel));

      //check for allocation
      if(new_thread == NULL){std::cerr << "Error allocating new thread" << std::endl; delete local_socket; delete new_tunnel; continue;}

      //lock vector (unlocks after scope exits)
      boost::mutex::scoped_lock the_lock(*reap_mutex);

      //push thread to vector and tunnel to vector
      threads.push_back(new_thread);
      tunnels.push_back(new_tunnel);

    }
  }
  //oh noes!
  catch(std::exception &e)
  {
    std::cerr << "=====ERROR=====" << std::endl
      << e.what() << std::endl;
  }
  halt();
  //wait for gc to end if we started it
  if(reap_thread != NULL){reap_thread->join();}
}
