#include "tunnel.cpp"

int main(int argc, char **argv)
{
  std::cout << "Hello" << std::endl;
  tunnel *new_tunnel;
  new_tunnel = new tunnel(argc, argv);
  delete new_tunnel;
  std::cout << "Goodbye" << std::endl;
  return 0;
}
