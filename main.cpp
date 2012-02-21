#include "tunnel.hpp"

void tunnel::reap(std::vector <boost::thread *> threads,boost::mutex *reap_mutex,boost::mutex *fin_mutex)
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
		for(std::size_t i = 0; i < threads.size(); i++)
		{
			//is thread done?
			if(threads[i]->joinable())
			{
				//join thread
				threads[i]->join();
	
				//delete thread
				delete threads[i];
	
				//erase thread pointer
				threads.erase(threads.begin() + i);
			}
		}
		
		//release memory if we have more than GARBAGE_RATE bytes allocated but unused by vector
	//	if(((threads.capacity() - threads.size) * sizeof(boost::thread *)) >= GARBAGE_RATE){threads.shrink_to_fit();}
		
	}
}

void tunnel::load_settings(char *file, char *clientserver) throw()
{
		std::ifstream settings(file);

		if(!settings.is_open()){throw std::exception();}


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

		//thread off reap() to kill dead threads, but first, mutex to keep reap alive
		boost::mutex::scoped_lock fin(*fin_mutex);

		reap_thread = new boost::thread(boost::bind(&tunnel::reap, this, threads, reap_mutex, fin_mutex));

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
	return 1;
}
