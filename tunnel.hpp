#ifnded TUNNEL_HEADER
#define TUNNEL_HEADER
//libraries
#include <iostream>
#include <fstream>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <vector>

//version
#define VERSION v1.2

//how many bytes of allocated but unused space in vector before i shrink
#define GARBAGE_RATE 1048576

using namespace std;

class tunnel
{
  private:
    boost::asio::io_service *io_service;
    boost::asio::ip::tcp::socket *remote_socket;
    boost::asio::ip::tcp::socket *local_socket;
    boost::asio::io_service::work *work;
    boost::mutex *local_mutex;
    boost::mutex *remote_mutex;
    boost::mutex alive;
    char *local_buffer;
    char *remote_buffer;
    char *local_temp;
    char *remote_temp;
    char *the_header;
    char *the_tail;
    char clientserver;
    bool local_bool;
    bool remote_bool;
    unsigned int receive_buffer_size;
    std::size_t header_size;
    std::size_t tail_size;
    std::size_t local_sent;
    std::size_t remote_sent;
    std::size_t local_to_send;
    std::size_t remote_to_send;
    std::size_t local_receive_size;
    std::size_t remote_receive_size;

  public:
    //make everything NULL
    void null();

    //*shudders* garbage collection
    void reap(std::vector <boost::thread *>* /*threads*/,std::vector <tunnel *>* /*tunnels*/, boost::mutex* /*reap_mutex*/, boost::mutex* /*fin_mutex*/);

    //die!!!
    ~tunnel();

    //to start tunnel up
    tunnel(int /*argc*/, char** /*argv*/);

    //each individual tunnel
    tunnel(boost::asio::ip::tcp::socket *, char *, char *, char, char *, char *, std::size_t, std::size_t, unsigned int) throw();

    //placeholder function, is called when something is sent, 2 functions so, if needed, logging can be done accurately
    void local_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/){}
    void remote_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/){}

    //i hate parsing text
    void http_server(char* /*IDONTWANNAPARSEYOU*/){}

    //add or remove header
    void hats(bool /*decrypt*/, char** /*temp*/, char* /*buffer*/, std::size_t &/*bytes_transferred*/) throw();

    void local_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred);
    void remote_receive(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/);

    //start the tunnel
    void run();

    //(sorta) end the tunnel
    void halt();

    //woo .conf
    void load_settings(char* /*file*/, char* /*clientserver*/) throw();

    //until death
    boost::mutex *is_alive(){return &alive;}

    //silly bugs
    void print(char *buffer, std::size_t length);
};

#endif
