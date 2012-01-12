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
		boost::asio::ip::tcp::socket *decrypt_me;
		boost::asio::ip::tcp::socket *encrypt_me;
		boost::asio::ip::tcp::socket *remote_socket;
		boost::asio::ip::tcp::socket *local_socket;
		boost::asio::io_service::work *work;
		boost::mutex *local_mutex;
		boost::mutex *remote_mutex;
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
		//make everything NULL
		void null();

		//*shudders* garbage collection
		void reap(std::vector <boost::thread *> /*threads*/, boost::mutex */*reap_mutex*/, boost::mutex */*fin_mutex*/);

		//die!!!
		~tunnel();

		//to start tunnel up
		tunnel(int /*argc*/, char **/*argv*/);

		//each individual tunnel
		tunnel(boost::asio::ip::tcp::socket *, char *, char *, char, char *, char *, std::size_t, std::size_t, unsigned int) throw();

		//placeholder function, is called when something is sent, 2 functions so, if needed, logging can be done accurately
		void local_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/){}
		void remote_send(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/){}

		//i hate parsing text
		void http_server(char */*IDONTWANNAPARSEYOU*/){}

		//add or remove header
		void hats(boost::asio::ip::tcp::socket */*socket*/, char */*temp*/, char */*buffer*/, std::size_t &/*bytes_transferred*/) throw();

		void local_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred);
		void remote_receive(const boost::system::error_code &/*error*/, std::size_t /*bytes_transferred*/);

		//start the tunnel
		void run();

		//(sorta) end the tunnel
		void halt();

		//woo .conf
		void load_settings(char */*file*/, char */*clientserver*/) throw();
};

tunnel::~tunnel()
{
	std::cout << "~tunnel()" << std::endl;

	if(work != NULL){io_service->stop(); delete work;}
	if(remote_socket != NULL){remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive); remote_socket->close(); delete remote_socket;}
	if(local_socket != NULL){local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive); local_socket->close(); delete local_socket;}
	if(io_service != NULL){delete io_service;}
	if(local_buffer != NULL){delete local_buffer;}
	if(remote_buffer != NULL){delete remote_buffer;}
}

tunnel::tunnel(boost::asio::ip::tcp::socket *socket, char *remote, char *remote_port, char clientserver, char *the_header, char *the_tail,
		std::size_t header_size, std::size_t tail_size, unsigned int receive_buffer_size) throw()
{
	//nulls
	null();

	try
	{
		//set up passed in variables and buffers
		this->the_header = the_header;
		this->the_tail = the_tail;
		this->header_size = header_size;
		this->tail_size = tail_size;
		this->receive_buffer_size = receive_buffer_size;

		local_buffer = new char[receive_buffer_size];
		remote_buffer = new char[receive_buffer_size];

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
			local_socket = encrypt_me = socket;
			remote_socket = decrypt_me = new boost::asio::ip::tcp::socket(*io_service);

			//receive less to be sure we can send full message with header and tail attached
			local_receive_size = receive_buffer_size - header_size - tail_size;
			remote_receive_size = receive_buffer_size;
		}
		else if (clientserver == 's')
		{
			local_socket = decrypt_me = socket;
			remote_socket = encrypt_me = new boost::asio::ip::tcp::socket(*io_service);

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
	boost::mutex::scoped_lock the_lock(*local_mutex);
	boost::mutex::scoped_lock another_lock(*remote_mutex);

	if(local_socket != NULL)
	{
		local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive);
		local_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send);
		local_socket->close();
	}
	if(remote_socket != NULL)
	{
		remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_receive);
		remote_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send);
		remote_socket->close();
	}

	//halt io_service
	io_service->stop();
}

void tunnel::hats(boost::asio::ip::tcp::socket *socket, char *temp, char *buffer, std::size_t &bytes_transferred) throw()
{
	return;

	if(socket == decrypt_me && header_size > 0)
	{
		//make sure packet is big enough
		if(bytes_transferred < (header_size + tail_size)){std::cerr << "Invalid packet" << std::endl; throw std::exception();}

		//ignore head and tail
		temp = &buffer[header_size];
		bytes_transferred -= (header_size + tail_size);
	}
	else if (header_size > 0)
	{
		//too much data!
		if(bytes_transferred > (receive_buffer_size - header_size - tail_size)){std::cerr << "Invalid packet" << std::endl; throw::std::exception();}

		for(std::size_t i = (bytes_transferred + header_size) - 1; i >= header_size; i--)
			buffer[i] = buffer[i-header_size];

		//add header
		strncpy(buffer, the_header, header_size);

		//add tail
		strncpy(&buffer[header_size+bytes_transferred], the_tail, tail_size);

		//send more
		bytes_transferred += (header_size + tail_size);

		//dont care about you
		temp = buffer;
	}
}
void tunnel::remote_receive(const boost::system::error_code &/*error*/, std::size_t bytes_transferred)
{
	try
	{
		if(remote_socket == encrypt_me){if(bytes_transferred == 0){halt(); return;}}
		else if(bytes_transferred == (header_size + tail_size)){halt(); return;}

		local_to_send = bytes_transferred;

		hats(remote_socket, remote_temp, remote_buffer, local_to_send);

		//grab mutex
		boost::mutex::scoped_lock the_lock(*remote_mutex);

		//make sure other side of tunnel has collapsed (and thus, called halt())
		if(local_socket->is_open())
		{
			//send to local
			local_sent = local_socket->send(boost::asio::buffer(remote_buffer, local_to_send));
			if(local_sent != local_to_send){std::cerr << "Failed to send everything" << std::endl;halt(); return;}
		}
		if(remote_socket->is_open())
		{
			//listen for more
			remote_socket->async_receive(boost::asio::buffer(remote_buffer, remote_receive_size),
				boost::bind(&tunnel::remote_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
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
		if(local_socket == encrypt_me){if(bytes_transferred == 0){halt(); return;}}
		else if(bytes_transferred == (header_size + tail_size)){halt(); return;}
		
		remote_to_send = bytes_transferred;

		hats(local_socket, local_temp, local_buffer, remote_to_send);

		//grab mutex
		boost::mutex::scoped_lock the_lock(*local_mutex);

		if(remote_socket->is_open())
		{
			//send to remote
			remote_sent = remote_socket->send(boost::asio::buffer(local_buffer, remote_to_send));
			if(remote_sent != remote_to_send){std::cerr << "Failed to send everything" << std::endl;halt(); return;}
		}
		if(local_socket->is_open())
		{
			//listen for more
			local_socket->async_receive(boost::asio::buffer(local_buffer, local_receive_size), 
				boost::bind(&tunnel::local_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}

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
	}
	catch(std::exception &e)
	{
		std::cout << "=====ERROR=====" << std::endl
			<< "Tunnel failed to run" << std::endl
			<< e.what() << std::endl;

			halt();
	}
}
//make all pointers NULL
void tunnel::null()
{
	io_service = NULL;
	decrypt_me = encrypt_me = remote_socket = local_socket = NULL;
	work = NULL;
	local_mutex = remote_mutex = NULL;
	local_buffer = remote_buffer = the_header = the_tail = NULL;
}
