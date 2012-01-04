#include "tunnel.hpp"

void tunnel::reap(std::vector <boost::thread *> threads)
{
	//keep alive
	while(true)
	{
		//dont run too fast
		boost::this_thread::sleep(boost::posix_time::seconds(10));
		
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

tunnel::tunnel(int argc, char **argv)
{

	try
	{
		if(argc != 5)
		{
			std::cerr << "Error, correct format is..." << std::endl
				<<"==for client==" << std::endl
					<< "   c [local port] [remote host] [remote port]" << std::endl
				<< "==for server==" << std::endl 
					<< "   s [local port] [remote host] [remote port]" << std::endl;
			return;
		}
		//vectors of our threads
		std::vector <boost::thread *> threads;

		//the io_service
		io_service = new (nothrow) boost::asio::io_service();

		//check for allocation
		if(io_service == NULL){std::cerr << "Error allocating io_service" << std::endl;}


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

		//create reap_mutex
		reap_mutex = new (nothrow) boost::mutex();

		if(reap_mutex == NULL){std::cerr << "Error, failed to allocate mutex" << std::endl;}

		//thread off reap() to kill dead threads
		boost::thread reap_thread(&tunnel::reap, this, threads);

		//tunnel pointer
		tunnel *new_tunnel;

		//thread pointer
		boost::thread *new_thread;

		//main loop for acception new connections
		while(true)
		{
			//make a new socket
			boost::asio::ip::tcp::socket socket(*io_service);

			//wait for incoming connection
			acceptor.accept(socket);

			std::cout << "Accepted..." << std::endl;

			//create tunnel
			new_tunnel = new (nothrow) tunnel(&socket, argv[3], argv[4], argv[1][0]);

			//check allocation
			if(new_tunnel == NULL){std::cerr << "Error allocating new tunnel" << std::endl;}

			//thread and create tunnel
			new_thread = new (nothrow) boost::thread(boost::bind(&tunnel::run, new_tunnel));

			//check for allocation
			if(new_thread == NULL){std::cerr << "Error allocating new thread" << std::endl;}

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
}

int main(int argc, char **argv)
{
	tunnel *new_tunnel;
	new_tunnel = new (nothrow) tunnel(argc, argv);
	delete new_tunnel;
	return 1;
}
