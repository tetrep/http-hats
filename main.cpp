#include "tunnel.hpp"

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

void tunnel::load_settings(char *file, char *clientserver) throw()
{
  try
  {
    int i;
    for(i = 0; file[i] != '\0'; i++){if(file[i] == '.'){break;}}

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
    char delim = 'Q';
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
/**/
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

tunnel::tunnel(int argc, char **argv)
{
  //nulls
  null();

  //reap_thread
  boost::thread *reap_thread = NULL;

  try
  {
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
  catch(std::exception &e)
  {
    std::cerr << "=====ERROR=====" << std::endl
      << e.what() << std::endl;
  }
  halt();
  if(reap_thread != NULL){reap_thread->join();}
}

int main(int argc, char **argv)
{
  std::cout << "Hello" << std::endl;
  tunnel *new_tunnel;
  new_tunnel = new (nothrow) tunnel(argc, argv);
  delete new_tunnel;
  std::cout << "Goodbye" << std::endl;
  return 0;
}
