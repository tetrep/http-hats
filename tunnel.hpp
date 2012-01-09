//libraries
#include <iostream>
#include <fstream>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <vector>

//version
#define VERSION 1.0
//how many bytes of allocated but unused space in vector before i shrink
#define GARBAGE_RATE 1048576

using namespace std;

class tunnel
{
	private:
		boost::asio::io_service *io_service;
		boost::asio::ip::tcp::socket *decrypt_me;
		boost::asio::ip::tcp::socket *encrypt_me;
		boost::asio::ip::tcp::socket *remote_socket;
		boost::asio::ip::tcp::socket *local_socket;
		boost::asio::io_service::work *work;
		boost::mutex *reap_mutex;
		char *local_buffer;
		char *remote_buffer;
		char *local_temp;
		char *remote_temp;
		char *the_header;
		char *the_tail;
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

		void reap(std::vector <boost::thread *> /*threads*/);
		tunnel(int /*argc*/, char **/*argv*/);
		void load_settings(char */*file*/, char */*clientserver*/) throw();

		~tunnel()
		{
			std::cout << "~tunnel()" << std::endl;

			if(work != NULL){io_service->stop(); delete work;}
			if(remote_socket != NULL){remote_socket->close(); delete remote_socket;}
			if(local_socket != NULL){local_socket->close(); delete local_socket;}
			if(io_service != NULL){delete io_service;}
			if(local_buffer != NULL){delete local_buffer;}
			if(remote_buffer != NULL){delete remote_buffer;}
		}
	
		tunnel(boost::asio::ip::tcp::socket *socket, char *remote, char *remote_port, char clientserver, char *the_header, char *the_tail,
			std::size_t header_size, std::size_t tail_size, unsigned int receive_buffer_size) throw()
		{
			try
			{
				//set up passed in variables and buffers
				this->the_header = the_header;
				this->the_tail = the_tail;
				this->header_size = header_size;
				this->tail_size = tail_size;
				this->receive_buffer_size = receive_buffer_size;
			
				local_buffer = new (nothrow) char[receive_buffer_size];
				remote_buffer = new (nothrow) char[receive_buffer_size];

				//check for allocation
				if(local_buffer == NULL || remote_buffer == NULL){std::cerr << "Error allocating buffer(s)" << std::endl; return;}
	
				//set up io_service
				io_service = new (nothrow) boost::asio::io_service();
	
				//check for allocation
				if(io_service == NULL){std::cerr << "Error allocating io_service" << std::endl; return;}
	
				//will resolve remote address
				boost::asio::ip::tcp::resolver resolver(*io_service);
				
				//build query to feed resolver
				boost::asio::ip::tcp::resolver::query query(remote, remote_port);
	
				//resolve!
				boost::asio::ip::tcp::resolver::iterator endpoints = resolver.resolve(query);

				//set up encrypt/decrypt and local/remote sockets 
				if(clientserver == 'c')
				{
					local_socket = encrypt_me = socket;
					remote_socket = decrypt_me = new (nothrow) boost::asio::ip::tcp::socket(*io_service);

					//receive less to be sure we can send full message with header and tail attached
					local_receive_size = receive_buffer_size - header_size - tail_size;
					remote_receive_size = receive_buffer_size;
				}
				else if (clientserver == 's')
				{
					local_socket = decrypt_me = socket;
					remote_socket = encrypt_me = new (nothrow) boost::asio::ip::tcp::socket(*io_service);

					//receive less to be sure we can send full message with header and tail attached
					local_receive_size = receive_buffer_size;
					remote_receive_size = receive_buffer_size - header_size - tail_size;
				}
	
				//check for allocation
				if(remote_socket == NULL){std::cerr << "Error allocating remote socket" << std::endl; return;}
	
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
	
		void local_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/)
		{

		}

		void remote_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/)
		{

		}

		void http_server(char *buffer)
		{

		}

		void hats(boost::asio::ip::tcp::socket *socket, char *temp, char *buffer, std::size_t &bytes_transferred) throw()
		{

				//remove header
				if(socket == decrypt_me)
				{
					//make sure packet has head + tail + data
					if(bytes_transferred < (header_size + tail_size + 1)){std::cerr << "Invalid packet" << std::endl; throw 1;}
					//ignore header and tail
					else{temp = &buffer[header_size]; bytes_transferred -= tail_size;}
				}

				//add header
				else
				{
					//dont care if sender sent too much, truncate
					for(std::size_t i = 0; i < bytes_transferred && i < (receive_buffer_size - header_size); i++)
						buffer[i+header_size] = buffer[i];
					//add header
					strncpy(buffer, the_header, header_size);
					//add tail
					strncpy(&buffer[header_size+bytes_transferred], the_tail, tail_size);
					//we now want to send more
					bytes_transferred += (header_size + tail_size);
					//dont need pointer offset
					temp = buffer;
				}
		}

		void local_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred)
		{
			try
			{
				hats(local_socket, local_temp, local_buffer, remote_to_send);

				//remote_to_send = bytes_transferred;

				//send to remote
				remote_sent = remote_socket->send(boost::asio::buffer(local_buffer, remote_to_send));
				if(remote_sent != remote_to_send){std::cerr << "Failed to send everything" << std::endl; io_service->stop(); return;}

				//listen for more
				local_socket->async_receive(boost::asio::buffer(local_buffer, local_receive_size), 
					boost::bind(&tunnel::local_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

			}
			catch(std::exception &e)
			{
				std::cerr << "=====ERROR=====" << std::endl
					<< "Tunnel Entrance Collapsed" << std::endl
					<< e.what() << std::endl;

				io_service->stop();
			}
		}

		//process data and send to local
		void remote_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred)
		{
			try
			{
				hats(local_socket, remote_temp, remote_buffer, local_to_send);
		
				//local_to_send = bytes_transferred;

				//sent to local
				local_sent = local_socket->send(boost::asio::buffer(remote_buffer, local_to_send));
				if(local_sent != local_to_send){std::cerr << "Failed to send everything" << std::endl; io_service->stop(); return;}

				//listen for more
				remote_socket->async_receive(boost::asio::buffer(remote_buffer, remote_receive_size),
					boost::bind(&tunnel::remote_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
			}
			catch(std::exception &e)
			{
				std::cerr << "=====ERROR=====" << std::endl
					<< "Tunnel Exit Collapsed" << std::endl
					<< e.what() << std::endl;

				io_service->stop();
			}
		}

		void run()
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
			}
			catch(std::exception &e)
			{
				std::cout << "=====ERROR=====" << std::endl
					<< "Tunnel failed to run" << std::endl
					<< e.what() << std::endl;

					io_service->stop();
			}
		}
};
